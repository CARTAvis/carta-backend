/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

#include <chrono>

#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/lattices/LRegions/LCRegionFixed.h>
#include <casacore/measures/Measures/Stokes.h>

#include "../ImageStats/StatsCalculator.h"
#include "../Logger/Logger.h"
#include "Util/FileSystem.h"
#include "Util/Image.h"

using namespace carta;

PvGenerator::PvGenerator(int file_id, const std::string& filename) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetPvFilename(filename);
}

bool PvGenerator::GetPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, const casacore::Matrix<float>& pv_data,
    const casacore::Quantity& offset_increment, int stokes, GeneratedImage& pv_image, std::string& message) {
    // Create PV image with input data. Returns PvResponse and GeneratedImage (generated file_id, pv filename, image).
    // Create casacore::TempImage
    casacore::IPosition pv_shape = pv_data.shape();
    if (!SetupPvImage(input_image, pv_shape, stokes, offset_increment, message)) {
        return false;
    }

    _image->put(pv_data);
    _image->flush();

    // Set returned image
    pv_image = GetGeneratedImage();
    return true;
}

std::string PvGenerator::GetPvFilename(const std::string& filename) {
    // image.ext -> image_pv.ext
    fs::path input_filepath(filename);

    // Assemble new filename
    auto pv_path = input_filepath.stem();
    pv_path += "_pv";
    pv_path += input_filepath.extension();

    return pv_path.string();
}

bool PvGenerator::SetupPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, casacore::IPosition& pv_shape, int stokes,
    const casacore::Quantity& offset_increment, std::string& message) {
    // Create coordinate system and temp image _image
    casacore::CoordinateSystem input_csys = input_image->coordinates();
    if (!input_csys.hasSpectralAxis()) {
        message = "Cannot generate PV image with no valid spectral axis.";
        return false;
    }

    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(input_csys, pv_shape, stokes, offset_increment);
    _image.reset(new casacore::TempImage<casacore::Float>(casacore::TiledShape(pv_shape), pv_csys));
    _image->setUnits(input_image->units());
    _image->setMiscInfo(input_image->miscInfo());
    _image->appendLog(input_image->logger());

    auto image_info = input_image->imageInfo();
    if (image_info.hasMultipleBeams()) {
        // Use first beam, per imageanalysis ImageCollapser
        auto beam = *(image_info.getBeamSet().getBeams().begin());
        image_info.removeRestoringBeam();
        image_info.setRestoringBeam(beam);
    }
    _image->setImageInfo(image_info);

    return true;
}

casacore::CoordinateSystem PvGenerator::GetPvCoordinateSystem(
    const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape, int stokes, const casacore::Quantity& offset_increment) {
    // Set PV coordinate system with LinearCoordinate and input coordinates for spectral and stokes
    casacore::CoordinateSystem csys;

    // Add linear coordinate (offset); needs to have 2 axes or pc matrix will fail in wcslib.
    // Will remove degenerate linear axis below
    casacore::Vector<casacore::String> name(2, "Offset");
    casacore::Vector<casacore::String> unit(2, offset_increment.getUnit());
    casacore::Vector<casacore::Double> crval(2, 0.0); // center offset is 0
    casacore::Vector<casacore::Double> inc(2, offset_increment.getValue());
    casacore::Matrix<casacore::Double> pc(2, 2, 1);
    pc(0, 1) = 0.0;
    pc(1, 0) = 0.0;
    casacore::Vector<casacore::Double> crpix(2, (pv_shape[0] - 1) / 2);
    casacore::LinearCoordinate linear_coord(name, unit, crval, inc, pc, crpix);
    csys.addCoordinate(linear_coord);

    // Add spectral or direction axis if its axis number is 2 (i.e., depth axis)
    if (input_csys.hasSpectralAxis() && input_csys.spectralAxisNumber() == 2) {
        csys.addCoordinate(input_csys.spectralCoordinate());
    } else if (input_csys.hasDirectionCoordinate()) {
        auto dir_axes = input_csys.directionAxesNumbers();
        if (dir_axes(0) == 2) {
            csys.addCoordinate(input_csys.directionCoordinate());
            csys.removeWorldAxis(3, 0.0); // Remove the second axis from direction axes
        } else if (dir_axes(1) == 2) {
            csys.addCoordinate(input_csys.directionCoordinate());
            csys.removeWorldAxis(2, 0.0); // Remove the first axis from direction axes
        } else {
            spdlog::error("Can not find depth axis from direction coordinate.");
        }
    } else {
        spdlog::error("Can not find depth axis from spectral or direction coordinates.");
    }

    // Add stokes coordinate if input image has one
    if (input_csys.hasPolarizationCoordinate()) {
        auto stokes_type = casacore::Stokes::type(stokes + 1);
        casacore::Vector<casacore::Int> types(1, stokes_type);
        casacore::StokesCoordinate stokes_coord(types);
        csys.addCoordinate(stokes_coord);
        pv_shape.append(casacore::IPosition(1, 1));
    }

    // Remove second linear axis
    csys.removeWorldAxis(1, 0.0);

    csys.setObsInfo(input_csys.obsInfo());

    return csys;
}

GeneratedImage PvGenerator::GetGeneratedImage() {
    // Set GeneratedImage struct
    GeneratedImage pv_image(_file_id, _name, _image);
    _image.reset(); // release ownership
    return pv_image;
}
