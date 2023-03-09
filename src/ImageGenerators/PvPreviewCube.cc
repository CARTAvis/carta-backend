/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvPreviewCube.h"

#include <casacore/images/Images/RebinImage.h>

namespace carta {

PvPreviewCube::PvPreviewCube(int file_id_, int region_id_, const AxisRange& spectral_range_, int rebin_xy_, int rebin_z_, int stokes_)
    : _cube_parameters(file_id_, region_id_, spectral_range_, rebin_xy_, rebin_z_, stokes_) {}

PvPreviewCube::PvPreviewCube(const PreviewCubeParameters& parameters) {
    _cube_parameters = parameters;
}

bool PvPreviewCube::HasSameParameters(const PreviewCubeParameters& parameters) {
    return _cube_parameters == parameters;
}

void PvPreviewCube::SetPreviewRegionOrigin(const casacore::IPosition& origin) {
    // Preview region's blc in source image
    _origin = origin;
}

casacore::IPosition PvPreviewCube::PreviewRegionOrigin() {
    // Preview region's blc in source image
    return _origin;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage() {
    // Holds nullptr if not set
    return _preview_image;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage(casacore::SubImage<float>& sub_image) {
    // Return saved preview image if sub_image is nullptr.
    // Or apply downsampling to SubImage to create preview image.

    // Reset stop flag
    _stop_cube = false;

    if (_preview_image) {
        return _preview_image;
    }

    auto ndim = sub_image.ndim();
    if (ndim == 0) {
        // SubImage not set so cannot make preview image
        return _preview_image; // null
    }

    // Zero if message field not set
    auto rebin_xy = std::max(_cube_parameters.rebin_xy, 1);
    auto rebin_z = std::max(_cube_parameters.rebin_z, 1);

    if (rebin_xy == 1 && rebin_z == 1) {
        // No downsampling, use create preview image from SubImage only
        _preview_image.reset(new casacore::SubImage<float>(sub_image));
        return _preview_image;
    }

    // TODO: Cancel rebinning?
    // Set rebin factors and make RebinImage (fails if image has multiple beams or spectral axis rebinned!)
    if (rebin_z == 1) {
        int x_axis(0), y_axis(1), z_axis(-1), stokes_axis(-1);
        casacore::Vector<int> xy_axes;
        if (sub_image.coordinates().hasDirectionCoordinate()) {
            xy_axes = sub_image.coordinates().directionAxesNumbers();
        } else if (sub_image.coordinates().hasLinearCoordinate()) {
            xy_axes = sub_image.coordinates().linearAxesNumbers();
        }

        // TODO: cases where xy not direction or linear?
        if (xy_axes.size() != 2) {
            return _preview_image; // null
        }
        x_axis = xy_axes[0];
        y_axis = xy_axes[1];
        z_axis = sub_image.coordinates().spectralAxisNumber();

        casacore::IPosition rebin_factors(sub_image.shape().size(), 1);
        rebin_factors[x_axis] = rebin_xy;
        rebin_factors[y_axis] = rebin_xy;
        rebin_factors[z_axis] = rebin_z;
        _preview_image.reset(new casacore::RebinImage<float>(sub_image, rebin_factors));
    }

    return _preview_image;
}

void PvPreviewCube::StopCube() {
    _stop_cube = true;
}

} // namespace carta
