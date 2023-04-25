/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/measures/Measures/Stokes.h>

#include "Util/Stokes.h"

using namespace carta;

PvGenerator::PvGenerator() : _file_id(0), _name("") {}

void PvGenerator::SetFileIdName(int file_id, int index, const std::string& filename, bool is_preview) {
    // Optional (not for preview) file id, and name for PV image
    _file_id = ((file_id + 1) * PV_ID_MULTIPLIER) - index;
    SetPvImageName(filename, index, is_preview);
}

bool PvGenerator::GetPvImage(std::shared_ptr<Frame>& frame, const casacore::Matrix<float>& pv_data, casacore::IPosition& pv_shape,
    const casacore::Quantity& offset_increment, int start_chan, int stokes, bool reverse, GeneratedImage& pv_image, std::string& message) {
    // Create PV image with input data.
    auto input_csys = frame->CoordinateSystem();
    if (!input_csys->hasSpectralAxis()) {
        // This should be checked before generating pv data.
        message = "Cannot generate PV image with no valid spectral axis.";
        return false;
    }

    // Convert spectral reference pixel value to world coordinates
    double spectral_refval, spectral_pixval(start_chan);
    input_csys->spectralCoordinate().toWorld(spectral_refval, spectral_pixval);

    // Create casacore::TempImage coordinate system and other image info
    auto image = SetupPvImage(frame->GetImage(), input_csys, pv_shape, stokes, offset_increment, spectral_refval, reverse, message);
    if (!image) {
        message = "PV image setup failed.";
        return false;
    }

    // Add data to TempImage
    image->put(pv_data);
    image->flush();

    // Set returned image
    pv_image = GeneratedImage(_file_id, _name, image);
    return true;
}

void PvGenerator::SetPvImageName(const std::string& filename, int index, bool is_preview) {
    // Index appended when multiple PV images shown for one input image
    // image.ext -> image_pv[index].ext
    // If is_preview: image.ext -> image_pv_preview[index].ext (index is preview_id)
    fs::path input_filepath(filename);

    // Assemble new filename
    auto pv_path = input_filepath.stem();
    pv_path += "_pv";
    if (is_preview) {
        pv_path += "_preview";
    }
    if (index > 0) {
        pv_path += std::to_string(index);
    }
    pv_path += input_filepath.extension();
    _name = pv_path.string();
}

std::shared_ptr<casacore::ImageInterface<casacore::Float>> PvGenerator::SetupPvImage(
    std::shared_ptr<casacore::ImageInterface<float>> input_image, std::shared_ptr<casacore::CoordinateSystem> input_csys,
    casacore::IPosition& pv_shape, int stokes, const casacore::Quantity& offset_increment, double spectral_refval, bool reverse,
    std::string& message) {
    // Create temporary image (no data) using input image.  Return casacore::TempImage.
    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(input_csys, pv_shape, stokes, offset_increment, spectral_refval, reverse);

    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image(
        new casacore::TempImage<casacore::Float>(casacore::TiledShape(pv_shape), pv_csys));

    image->setUnits(input_image->units());
    image->setMiscInfo(input_image->miscInfo());
    auto image_info = input_image->imageInfo();
    if (image_info.hasMultipleBeams()) {
        // Use first beam, per imageanalysis ImageCollapser
        auto beam = *(image_info.getBeamSet().getBeams().begin());
        image_info.removeRestoringBeam();
        image_info.setRestoringBeam(beam);
    }
    image->setImageInfo(image_info);

    return image;
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

    // Set spectral coordinate
    casacore::SpectralCoordinate spectral_coord = input_csys->spectralCoordinate();
    casacore::Vector<casacore::Double> refval(1, spectral_refval);
    casacore::Vector<casacore::Double> refpix(1, 0.0);
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
        auto casacore_stokes_type = StokesTypesToCasacore[stokes];
        casacore::Vector<casacore::Int> types(1, casacore_stokes_type);
        casacore::StokesCoordinate stokes_coord(types);
        pv_csys.addCoordinate(stokes_coord);
        pv_shape.append(casacore::IPosition(1, 1));
    }

    // Remove second linear axis
    pv_csys.removeWorldAxis(offset_axis + 1, 0.0);

    pv_csys.setObsInfo(input_csys->obsInfo());

    return pv_csys;
}
