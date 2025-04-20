/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

PvGenerator::PvGenerator() : _name("") {}

void PvGenerator::SetFileName(int index, const std::string& filename, bool is_preview) {
    // Optional (not for preview) file id, and name for PV image
    SetPvImageName(filename, index, is_preview);
}

bool PvGenerator::GetPvImage(const std::shared_ptr<Frame>& frame, const casacore::Matrix<float>& pv_data, casacore::IPosition& pv_shape,
    PositionAxisType axis_type, const casacore::Quantity& position_increment, int start_chan, int stokes, bool reverse,
    GeneratedImage& pv_image, std::string& message) {
    // Create PV image with input data.
    auto input_csys = frame->CoordinateSystem();
    if (!input_csys->hasSpectralAxis()) {
        // This should be checked before generating pv data.
        message = "Cannot generate PV image with no valid spectral axis.";
        return false;
    }

    // Position axis
    int linear_axis = (reverse ? 1 : 0);
    std::string position_name;
    double position_refval(0.0), position_refpix(0.0);
    if (axis_type == OFFSET) {
        // refval 0 at center pixel
        position_name = "Offset";
        position_refpix = (pv_shape[linear_axis] - 1) / 2;
    } else {
        // refval 0 at first pixel
        position_name = "Distance";
    }

    // Spectral axis: Convert spectral reference pixel value to world coordinates
    double spectral_refval, spectral_pixval(start_chan);
    input_csys->spectralCoordinate().toWorld(spectral_refval, spectral_pixval);

    // Create casacore::TempImage coordinate system and other image info
    auto image = SetupPvImage(frame->GetImage(), input_csys, pv_shape, position_name, position_increment, position_refval, position_refpix,
        spectral_refval, stokes, reverse, message);
    if (!image) {
        message = "PV image setup failed.";
        return false;
    }

    // Add data to TempImage
    image->put(pv_data);
    image->flush();

    // Set returned image
    pv_image = GeneratedImage(_name, image);
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
    const std::shared_ptr<casacore::ImageInterface<float>>& input_image, const std::shared_ptr<casacore::CoordinateSystem>& input_csys,
    casacore::IPosition& pv_shape, const std::string& position_name, const casacore::Quantity& position_increment, double position_refval,
    double position_refpix, double spectral_refval, int stokes, bool reverse, std::string& message) {
    // Create temporary image (no data) using input image.  Return casacore::TempImage.
    // Position axis
    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(
        input_csys, pv_shape, position_name, position_increment, position_refval, position_refpix, spectral_refval, stokes, reverse);

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

casacore::CoordinateSystem PvGenerator::GetPvCoordinateSystem(const std::shared_ptr<casacore::CoordinateSystem>& input_csys,
    casacore::IPosition& pv_shape, const std::string& position_name, const casacore::Quantity& position_increment, double position_refval,
    double position_refpix, double spectral_refval, int stokes, bool reverse) {
    // Set PV coordinate system with LinearCoordinate and input coordinates for spectral and stokes
    casacore::CoordinateSystem pv_csys;

    // Add linear coordinate (offset or distance); needs to have 2 axes or pc matrix will fail in wcslib.
    // Will remove degenerate linear axis below
    casacore::Vector<casacore::String> name(2, position_name);
    casacore::Vector<casacore::String> unit(2, position_increment.getUnit());
    casacore::Vector<casacore::Double> refval(2, position_refval);
    casacore::Vector<casacore::Double> inc(2, position_increment.getValue());
    casacore::Matrix<casacore::Double> pc(2, 2, 1);
    pc(0, 1) = 0.0;
    pc(1, 0) = 0.0;
    casacore::Vector<casacore::Double> refpix(2, position_refpix);
    casacore::LinearCoordinate linear_coord(name, unit, refval, inc, pc, refpix);

    // Set spectral coordinate
    auto& input_spectral_coord = input_csys->spectralCoordinate();
    auto freq_type = input_spectral_coord.frequencySystem();
    auto freq_inc = input_spectral_coord.increment()(0);
    auto rest_freq = input_spectral_coord.restFrequency();
    casacore::Double spectral_refpix(0.0);
    casacore::SpectralCoordinate spectral_coord(freq_type, spectral_refval, freq_inc, spectral_refpix, rest_freq);

    // Add offset and spectral coordinates in axis order
    int linear_axis(0);
    if (reverse) {
        pv_csys.addCoordinate(spectral_coord);
        pv_csys.addCoordinate(linear_coord);
        linear_axis = 1;
    } else {
        pv_csys.addCoordinate(linear_coord);
        pv_csys.addCoordinate(spectral_coord);
    }

    // Add stokes coordinate if input image has one
    if (input_csys->hasPolarizationCoordinate()) {
        auto casacore_stokes_type = Stokes::ToCasa(Stokes::Get(stokes));
        casacore::Vector<casacore::Int> types(1, casacore_stokes_type);
        casacore::StokesCoordinate stokes_coord(types);
        pv_csys.addCoordinate(stokes_coord);
        pv_shape.append(casacore::IPosition(1, 1));
    }

    // Remove second linear axis
    pv_csys.removeWorldAxis(linear_axis + 1, 0.0);

    pv_csys.setObsInfo(input_csys->obsInfo());

    return pv_csys;
}
