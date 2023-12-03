/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageState.h"
#include "Logger/Logger.h"

using namespace carta;

ImageState::ImageState(uint32_t session_id, std::shared_ptr<FileLoader> loader, int default_z, std::string& error)
    : width(0),
      height(0),
      depth(1),
      num_stokes(1),
      x_axis(0),
      y_axis(1),
      z_axis(-1),
      spectral_axis(-1),
      stokes_axis(-1),
      z(default_z),
      stokes(DEFAULT_STOKES),
      valid(false) {
    if (!loader) {
        spdlog::error("Session {}: file loader does not exist.", session_id);
        return;
    }

    std::string message;
    std::vector<int> spatial_axes;
    std::vector<int> render_axes;
    if (!loader->FindCoordinateAxes(image_shape, spatial_axes, spectral_axis, stokes_axis, render_axes, z_axis, message)) {
        error = fmt::format("Cannot determine file shape. {}", message);
        spdlog::error("Session {}: {}", session_id, error);
        return;
    }

    x_axis = render_axes[0];
    y_axis = render_axes[1];
    width = image_shape(x_axis);
    height = image_shape(y_axis);
    depth = (z_axis >= 0 ? image_shape(z_axis) : 1);
    num_stokes = (stokes_axis >= 0 ? image_shape(stokes_axis) : 1);
    valid = true;
}

bool ImageState::CheckZ(int z_) const {
    return (z_ >= 0 && z_ < depth);
}

bool ImageState::CheckStokes(int stokes_) const {
    return ((stokes_ >= 0 && stokes_ < num_stokes) || IsComputedStokes(stokes_));
}

bool ImageState::ZStokesChanged(int z_, int stokes_) const {
    return (z_ != z || stokes_ != stokes);
}

void ImageState::SetCurrentZ(int z_) {
    z = z_;
}

void ImageState::SetCurrentStokes(int stokes_) {
    stokes = stokes_;
}

void ImageState::CheckCurrentZ(int& z_) const {
    if (z_ == CURRENT_Z) {
        z_ = z;
    }
}

void ImageState::CheckCurrentStokes(int& stokes_) const {
    if (stokes_ == CURRENT_STOKES) {
        stokes_ = stokes;
    }
}

bool ImageState::IsCurrentChannel(int z_, int stokes_) const {
    CheckCurrentZ(z_);
    CheckCurrentStokes(stokes_);
    return (z_ == z && stokes_ == stokes);
}
