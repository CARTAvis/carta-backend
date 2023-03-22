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

int PvPreviewCube::GetStokes() {
    return _cube_parameters.stokes;
}

void PvPreviewCube::SetSourceFileName(const std::string& name) {
    _source_filename = name;
}

std::string PvPreviewCube::GetSourceFileName() {
    return _source_filename;
}

void PvPreviewCube::SetPreviewRegionOrigin(const casacore::IPosition& origin) {
    // Preview region's blc in source image
    _origin = origin;
}

bool PvPreviewCube::UsePreviewRegionOrigin() {
    // Whether to set pv cut using origin, or need conversion
    return _cube_parameters.rebin_xy <= 1;
}

casacore::IPosition PvPreviewCube::PreviewRegionOrigin() {
    // Preview region's blc in source image
    return _origin;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage() {
    // Returns cached preview image; nullptr if not set
    return _preview_image;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage(casacore::SubImage<float>& sub_image, std::string& error) {
    // Input SubImage is preview region, spectral range, and stokes applied to source image.
    // Apply downsampling to this subimage if needed.
    // Returns false if sub_image not set, preview image fails, or cancelled.
    _stop_cube = false;

    if (_preview_image) {
        return _preview_image;
    }

    if (sub_image.ndim() == 0) {
        return _preview_image;
    }

    // Rebin is zero if request message fields not set, reset to 1.
    auto rebin_xy = std::max(_cube_parameters.rebin_xy, 1);
    auto rebin_z = std::max(_cube_parameters.rebin_z, 1);

    if (rebin_xy > 1 && rebin_z > 1) {
        try {
            // Make casacore::RebinImage
            int x_axis(0), y_axis(1), z_axis(-1), stokes_axis(-1);
            casacore::Vector<int> xy_axes;
            if (sub_image.coordinates().hasDirectionCoordinate()) {
                xy_axes = sub_image.coordinates().directionAxesNumbers();
            } else if (sub_image.coordinates().hasLinearCoordinate()) {
                xy_axes = sub_image.coordinates().linearAxesNumbers();
            }

            if (xy_axes.size() != 2) {
                error = "Cannot find xy spatial axes to rebin.";
                return _preview_image;
            }

            x_axis = xy_axes[0];
            y_axis = xy_axes[1];
            z_axis = sub_image.coordinates().spectralAxisNumber();

            casacore::IPosition rebin_factors(sub_image.ndim(), 1);
            rebin_factors[x_axis] = rebin_xy;
            rebin_factors[y_axis] = rebin_xy;
            rebin_factors[z_axis] = rebin_z;
            _preview_image.reset(new casacore::RebinImage<float>(sub_image, rebin_factors));
        } catch (const casacore::AipsError& err) {
            error = err.getMesg();
            return _preview_image;
        }
    } else {
        // No downsampling, create preview image from SubImage only
        _preview_image.reset(new casacore::SubImage<float>(sub_image));
    }

    if (!_stop_cube) {
        // Cache cube data (remove degenerate stokes axis)
        _cube_data = _preview_image->get(true);
    }

    return _preview_image;
}

bool PvPreviewCube::GetRegionProfile(std::shared_ptr<casacore::LCRegion> region, const casacore::ArrayLattice<casacore::Bool>& mask,
    std::vector<float>& profile, double& num_pixels) {
    // Set spectral profile and maximum number of pixels for region.
    // Returns false if no preview image or region cannot be applied.
    if (!region || !_preview_image) {
        return false;
    }

    // If cancelled during preview image, load data now.
    if (_cube_data.empty()) {
        _cube_data = _preview_image->get(true); // remove degenerate stokes axis
    }

    auto bounding_box = region->boundingBox();
    auto box_start = bounding_box.start();
    auto box_length = bounding_box.length();

    // Initialize profile
    size_t nchan = box_length(2);
    profile.resize(nchan, NAN);
    std::vector<double> npix_per_chan(nchan, 0.0);

    for (size_t ichan = 0; ichan < nchan; ++ichan) {
        double chan_sum(0.0);
        for (size_t ix = 0; ix < box_length[0]; ++ix) {
            for (size_t iy = 0; iy < box_length[1]; ++iy) {
                // Accumulate if pixel in region (mask=true) and is not NAN or inf
                if (!mask.getAt(casacore::IPosition(2, ix, iy))) {
                    continue;
                }

                float data_val = _cube_data(casacore::IPosition(3, ix + box_start[0], iy + box_start[1], ichan));
                if (std::isfinite(data_val)) {
                    chan_sum += data_val;
                    ++npix_per_chan[ichan];
                }
            }
        }

        // Calculate per-channel mean
        if (npix_per_chan[ichan] > 0) {
            profile[ichan] = chan_sum / npix_per_chan[ichan];
        }
    }

    num_pixels = *max_element(npix_per_chan.begin(), npix_per_chan.end());
    return true;
}

void PvPreviewCube::StopCube() {
    _stop_cube = true;
}

} // namespace carta
