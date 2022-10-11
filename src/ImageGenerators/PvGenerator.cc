/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

#include <chrono>

#include <casacore/casa/Quanta/UnitMap.h>
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

PvGenerator::PvGenerator(int file_id, const std::string& filename, int index = 0) {
    _file_id = ((file_id + 1) * PV_ID_MULTIPLIER) - index;
    _name = GetPvFilename(filename, index);
}

bool PvGenerator::GetPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image,
    std::shared_ptr<casacore::CoordinateSystem> input_csys, const casacore::Matrix<float>& pv_data,
    const casacore::Quantity& offset_increment, double spectral_refval, int stokes, bool reverse, GeneratedImage& pv_image,
    std::string& message) {
    // Create PV image with input data. Returns PvResponse and GeneratedImage (generated file_id, pv filename, image).
    // Create casacore::TempImage
    casacore::IPosition pv_shape = pv_data.shape();
    if (!SetupPvImage(input_image, input_csys, pv_shape, stokes, offset_increment, spectral_refval, reverse, message)) {
        return false;
    }

    _image->put(pv_data);
    _image->flush();

    // Set returned image
    pv_image = GetGeneratedImage();
    return true;
}

std::string PvGenerator::GetPvFilename(const std::string& filename, int index) {
    // Index appended when multiple PV images shown for one input image
    // image.ext -> image_pv[index].ext
    fs::path input_filepath(filename);

    // Assemble new filename
    auto pv_path = input_filepath.stem();
    pv_path += "_pv";

    if (index > 0) {
        pv_path += std::to_string(index);
    }

    pv_path += input_filepath.extension();

    return pv_path.string();
}

bool PvGenerator::SetupPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image,
    std::shared_ptr<casacore::CoordinateSystem> input_csys, casacore::IPosition& pv_shape, int stokes,
    const casacore::Quantity& offset_increment, double spectral_refval, bool reverse, std::string& message) {
    // Create coordinate system and temp image _image
    if (!input_csys->hasSpectralAxis()) {
        message = "Cannot generate PV image with no valid spectral axis.";
        return false;
    }

    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(input_csys, pv_shape, stokes, offset_increment, spectral_refval, reverse);
    _image.reset(new casacore::TempImage<casacore::Float>(casacore::TiledShape(pv_shape), pv_csys));
    _image->setUnits(input_image->units());
    _image->setMiscInfo(input_image->miscInfo());

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

casacore::CoordinateSystem PvGenerator::GetPvCoordinateSystem(std::shared_ptr<casacore::CoordinateSystem> input_csys,
    casacore::IPosition& pv_shape, int stokes, const casacore::Quantity& offset_increment, double spectral_refval, bool reverse) {
    // Set PV coordinate system with LinearCoordinate and input coordinates for spectral and stokes
    casacore::CoordinateSystem pv_csys;
    int offset_axis = reverse ? 1 : 0;

    // Add linear coordinate (offset); needs to have 2 axes or pc matrix will fail in wcslib.
    // Will remove degenerate linear axis below
    casacore::Vector<casacore::String> name(2, "Offset");
    casacore::Vector<casacore::String> unit(2, offset_increment.getUnit());
    casacore::Vector<casacore::Double> crval(2, 0.0); // center offset is 0
    casacore::Vector<casacore::Double> inc(2, offset_increment.getValue());
    casacore::Matrix<casacore::Double> pc(2, 2, 1);
    pc(0, 1) = 0.0;
    pc(1, 0) = 0.0;
    casacore::Vector<casacore::Double> crpix(2, (pv_shape[offset_axis] - 1) / 2);
    casacore::LinearCoordinate linear_coord(name, unit, crval, inc, pc, crpix);

    // Set spectral reference value (changes if spectral range)
    casacore::Vector<casacore::Double> refval(1, spectral_refval);
    casacore::Vector<casacore::Double> refpix(1, 0.0);
    casacore::SpectralCoordinate spectral_coord(input_csys->spectralCoordinate());
    spectral_coord.setReferenceValue(refval);
    spectral_coord.setReferencePixel(refpix);

    // Add offset and spectral coordinates
    if (reverse) {
        pv_csys.addCoordinate(spectral_coord);
        pv_csys.addCoordinate(linear_coord);
    } else {
        pv_csys.addCoordinate(linear_coord);
        pv_csys.addCoordinate(spectral_coord);
    }

    // Add stokes coordinate if input image has one
    if (input_csys->hasPolarizationCoordinate()) {
        auto stokes_type = casacore::Stokes::type(stokes + 1);
        casacore::Vector<casacore::Int> types(1, stokes_type);
        casacore::StokesCoordinate stokes_coord(types);
        pv_csys.addCoordinate(stokes_coord);
        pv_shape.append(casacore::IPosition(1, 1));
    }

    // Remove second linear axis
    pv_csys.removeWorldAxis(offset_axis + 1, 0.0);

    pv_csys.setObsInfo(input_csys->obsInfo());

    return pv_csys;
}

GeneratedImage PvGenerator::GetGeneratedImage() {
    // Set GeneratedImage struct
    GeneratedImage pv_image(_file_id, _name, _image);
    _image.reset(); // release ownership
    return pv_image;
}
