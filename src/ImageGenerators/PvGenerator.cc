/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

PvGenerator::PvGenerator(int file_id, const std::string& filename) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetPvFilename(filename);
}

bool PvGenerator::GetPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, const casacore::Matrix<float>& pv_data,
    double offset_increment, int stokes, GeneratedImage& pv_image, std::string& message) {
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
    double offset_increment, std::string& message) {
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
    const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape, int stokes, double offset_increment) {
    // Set PV coordinate system with LinearCoordinate and input coordinates for spectral and stokes
    casacore::CoordinateSystem csys;
    casacore::Quantity increment = AdjustIncrementUnit(offset_increment, pv_shape(0));

    // Add linear coordinate (offset); needs to have 2 axes or pc matrix will fail in wcslib.
    // Will remove degenerate linear axis below
    casacore::Vector<casacore::String> name(2, "Offset");
    casacore::Vector<casacore::String> unit(2, increment.getUnit());
    casacore::Vector<casacore::Double> crval(2, 0.0); // center offset is 0
    casacore::Vector<casacore::Double> inc(2, increment.getValue());
    casacore::Matrix<casacore::Double> pc(2, 2, 1);
    pc(0, 1) = 0.0;
    pc(1, 0) = 0.0;
    casacore::Vector<casacore::Double> crpix(2, (pv_shape[0] - 1) / 2);
    casacore::LinearCoordinate linear_coord(name, unit, crval, inc, pc, crpix);
    csys.addCoordinate(linear_coord);

    // Add spectral coordinate
    csys.addCoordinate(input_csys.spectralCoordinate());

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

    return csys;
}

casacore::Quantity PvGenerator::AdjustIncrementUnit(double offset_increment, size_t num_offsets) {
    // Given offset increment in arcsec, adjust to:
    // - milliarcsec if length < 2 milliarcsec
    // - arcsec if 2 milliarcsec <= length < 2 arcmin
    // - arcminute if 2 arcmin <= length < 2 deg
    // - deg if 2 deg <= length
    // Returns increment as a Quantity with value and unit
    casacore::Quantity increment(offset_increment, "arcsec");

    auto offset_length = offset_increment * num_offsets;

    if ((offset_length * 1.0e3) < 2.0) { // milliarcsec
        increment = increment.get("marcsec");
    } else if ((offset_length / 60.0) >= 2.0) { // arcmin
        if ((offset_length / 3600.0) < 2.0) {   // deg
            increment = increment.get("arcmin");
        } else {
            increment = increment.get("deg");
        }
    }

    return increment;
}

GeneratedImage PvGenerator::GetGeneratedImage() {
    // Set GeneratedImage struct
    GeneratedImage pv_image(_file_id, _name, _image);
    _image.reset(); // release ownership
    return pv_image;
}
