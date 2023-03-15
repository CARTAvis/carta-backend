/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvPreviewCube.h"

#include <casacore/images/Images/RebinImage.h>

namespace carta {

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
    if (_preview_image && !_stop_cube && _cube_data.empty()) {
        // Usually loaded after preview image created, unless cancelled
        _cube_data = _preview_image->get(true); // remove degenerate stokes axis
    }

    return _preview_image;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage(casacore::SubImage<float>& sub_image, std::string& error) {
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
    bool rebin = rebin_xy > 1 && rebin_z > 1;

    if (!rebin) {
        // No downsampling, create preview image from SubImage only
        _preview_image.reset(new casacore::SubImage<float>(sub_image));
    } else {
        // Set rebin factors and make RebinImage (fails if image has multiple beams and spectral axis rebinned!)
        int x_axis(0), y_axis(1), z_axis(-1), stokes_axis(-1);
        casacore::Vector<int> xy_axes;
        if (sub_image.coordinates().hasDirectionCoordinate()) {
            xy_axes = sub_image.coordinates().directionAxesNumbers();
        } else if (sub_image.coordinates().hasLinearCoordinate()) {
            xy_axes = sub_image.coordinates().linearAxesNumbers();
        }

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
        try {
            _preview_image.reset(new casacore::RebinImage<float>(sub_image, rebin_factors));
        } catch (const casacore::AipsError& err) {
            error = err.getMesg();
            return _preview_image;
        }
    }

    if (_preview_image && !_stop_cube) {
        _cube_data = _preview_image->get(true); // remove degenerate stokes axis
    }

    return _preview_image;
}

bool PvPreviewCube::GetRegionProfile(std::shared_ptr<casacore::LCRegion> region, const casacore::ArrayLattice<casacore::Bool>& mask,
    std::vector<float>& profile, double& num_pixels) {
    // Return spectral profile for region.  Returns false if no preview cube or region cannot be applied.
    if (!region || !_preview_image || _cube_data.empty()) {
        return false;
    }

    // Get region bounding box (casacore::Slicer)
    auto bounding_box = region->boundingBox();
    auto box_start = bounding_box.start();
    auto box_length = bounding_box.length();
    // Accumulators
    size_t nchan = _cube_data.shape()[2];
    std::vector<double> sum(nchan, 0.0);
    std::vector<double> npix_per_chan(nchan, 0.0);
    profile.resize(nchan, NAN);

    for (size_t ichan = 0; ichan < nchan; ++ichan) {
        // Iterate through xy pixels for per-channel sum and number of values
        for (size_t ix = 0; ix < box_length[0]; ++ix) {
            for (size_t iy = 0; iy < box_length[1]; ++iy) {
                if (!mask.getAt(casacore::IPosition(2, ix, iy))) {
                    continue;
                }

                float data_val = _cube_data(casacore::IPosition(3, ix + box_start[0], iy + box_start[1], ichan));
                if (std::isfinite(data_val)) {
                    sum[ichan] += data_val;
                    ++npix_per_chan[ichan];
                }
            }
        }
        // Calculate per-channel mean
        profile[ichan] = sum[ichan] / npix_per_chan[ichan];
    }

    num_pixels = *max_element(npix_per_chan.begin(), npix_per_chan.end());
    return true;
}

void PvPreviewCube::StopCube() {
    _stop_cube = true;
}

} // namespace carta
