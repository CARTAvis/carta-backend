/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvPreviewCube.h"

#include <cmath>
#include <valarray>

#include <casacore/images/Images/RebinImage.h>

#include "DataStream/Smoothing.h"
#include "Timer/Timer.h"

#define LOAD_DATA_PROGRESS_INTERVAL 1000

namespace carta {

PvPreviewCube::PvPreviewCube(const PreviewCubeParameters& parameters)
    : _cube_parameters(parameters),
      _origin(casacore::IPosition(2, 0, 0)),
      _stop_cube(false),
      _cancel_message("PV image preview cancelled.") {}

PreviewCubeParameters PvPreviewCube::parameters() {
    return _cube_parameters;
}

bool PvPreviewCube::HasSameParameters(const PreviewCubeParameters& parameters) {
    return _cube_parameters == parameters;
}

bool PvPreviewCube::HasFileId(int file_id) {
    return _cube_parameters.file_id == file_id;
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

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage(
    GeneratorProgressCallback progress_callback, bool& cancel, std::string& message) {
    // Returns cached preview image; nullptr if not set
    if (_preview_image && !CubeLoaded()) {
        LoadCubeData(progress_callback, cancel);
        if (cancel) {
            message = _cancel_message;
        }
    }
    return _preview_image;
}

std::shared_ptr<casacore::ImageInterface<float>> PvPreviewCube::GetPreviewImage(
    casacore::SubImage<float>& sub_image, GeneratorProgressCallback progress_callback, bool& cancel, std::string& message) {
    // Input SubImage is preview region, spectral range, and stokes applied to source image.
    // Apply downsampling to this subimage if needed.
    // Returns false if sub_image not set, preview image fails, or cancelled.
    cancel = false;

    if (_preview_image) {
        // Image already created, load data if cancelled
        if (!CubeLoaded()) {
            LoadCubeData(progress_callback, cancel);
        }
        if (cancel) {
            message = _cancel_message;
        }
        return _preview_image;
    }

    if (sub_image.ndim() == 0) {
        message = "Preview region failed.";
        return _preview_image;
    }

    // For data access, instead of RebinImage (too slow)
    _preview_subimage = sub_image;

    if (DoRebin()) {
        try {
            // Make casacore::RebinImage for headers only
            int x_axis(0), y_axis(1), z_axis(-1);
            casacore::Vector<int> xy_axes;
            if (sub_image.coordinates().hasDirectionCoordinate()) {
                xy_axes = sub_image.coordinates().directionAxesNumbers();
            } else if (sub_image.coordinates().hasLinearCoordinate()) {
                xy_axes = sub_image.coordinates().linearAxesNumbers();
            }

            if (xy_axes.size() != 2) {
                message = "Cannot find xy spatial axes to rebin.";
                return _preview_image;
            }

            x_axis = xy_axes[0];
            y_axis = xy_axes[1];
            z_axis = sub_image.coordinates().spectralAxisNumber();

            casacore::IPosition rebin_factors(sub_image.ndim(), 1);
            rebin_factors[x_axis] = _cube_parameters.rebin_xy;
            rebin_factors[y_axis] = _cube_parameters.rebin_xy;
            rebin_factors[z_axis] = _cube_parameters.rebin_z;
            _preview_image.reset(new casacore::RebinImage<float>(sub_image, rebin_factors));
        } catch (const casacore::AipsError& err) {
            message = err.getMesg();
            return _preview_image;
        }
    } else {
        // No downsampling, create preview image from SubImage only
        _preview_image.reset(new casacore::SubImage<float>(sub_image));
    }

    LoadCubeData(progress_callback, cancel);
    if (cancel) {
        message = _cancel_message;
    }

    return _preview_image;
}

RegionState PvPreviewCube::GetPvCutRegion(const RegionState& source_region_state, int preview_frame_id) {
    // Calculate line control points in downsampled preview image from pv cut in source image
    std::vector<CARTA::Point> preview_line_points;

    // Subtract bottom left corner of preview region and apply rebin
    float blc_x(_origin[0]), blc_y(_origin[1]);
    auto rebin_xy = _cube_parameters.rebin_xy;
    CARTA::Point line_point;

    for (auto& point : source_region_state.control_points) {
        line_point.set_x((point.x() - blc_x) / rebin_xy);
        line_point.set_y((point.y() - blc_y) / rebin_xy);
        preview_line_points.push_back(line_point);
    }

    return RegionState(preview_frame_id, CARTA::RegionType::LINE, preview_line_points, source_region_state.rotation);
}

bool PvPreviewCube::GetRegionProfile(const casacore::Slicer& region_bounding_box, const casacore::ArrayLattice<casacore::Bool>& mask,
    GeneratorProgressCallback progress_callback, std::vector<float>& profile, double& num_pixels, bool& cancel, std::string& message) {
    // Set spectral profile and maximum number of pixels for region.
    // Returns false if no preview image
    if (!_preview_image || !CubeLoaded()) {
        return false;
    }

    auto box_start = region_bounding_box.start();
    auto box_length = region_bounding_box.length();

    // Initialize profile
    size_t nchan = box_length(_preview_image->coordinates().spectralAxisNumber());
    profile.resize(nchan, NAN);
    std::vector<double> npix_per_chan(nchan, 0.0);
    auto data_shape = _cube_data.shape();
    auto mask_shape = mask.shape();

    for (size_t ichan = 0; ichan < nchan; ++ichan) {
        double chan_sum(0.0);
        for (size_t ix = 0; ix < box_length[0]; ++ix) {
            for (size_t iy = 0; iy < box_length[1]; ++iy) {
                // Accumulate if pixel in region (mask=true) and is not NAN or inf
                // Check index into mask and data cube
                casacore::IPosition mask_pos(2, ix, iy);
                casacore::IPosition data_pos(3, ix + box_start[0], iy + box_start[1], ichan);
                if (mask_pos > mask_shape || data_pos > data_shape) {
                    message = "Region profile failed accessing data or mask.";
                    profile.clear();
                    return false;
                }

                if (!mask.getAt(mask_pos)) {
                    continue;
                }

                float data_val = _cube_data(data_pos);

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

bool PvPreviewCube::DoRebin() {
    return _cube_parameters.rebin_xy > 1 || _cube_parameters.rebin_z > 1;
}

void PvPreviewCube::LoadCubeData(GeneratorProgressCallback progress_callback, bool& cancel) {
    // Cache preview image data in memory
    // First check if user cancelled.
    if (_stop_cube) {
        cancel = true;
        _stop_cube = false; // reset for next preview
        return;
    }

    Timer t;
    if (DoRebin()) {
        casacore::Array<float> carta_cube_data;
        int spectral_axis(_preview_subimage.coordinates().spectralAxisNumber());
        auto subimage_shape = _preview_subimage.shape();

        // Dimensions
        auto width = subimage_shape(0);
        auto height = subimage_shape(1);
        auto nchan = subimage_shape(spectral_axis);
        auto chan_size = width * height;

        // Channel slicer start and length
        casacore::IPosition start(subimage_shape.size(), 0);
        casacore::IPosition length(subimage_shape);
        length(spectral_axis) = 1;

        // Rebin shape: same shape as casacore::RebinImage
        auto rebin_xy = _cube_parameters.rebin_xy;
        auto rebin_z = _cube_parameters.rebin_z;
        size_t rebin_width = std::ceil((float)width / (float)rebin_xy);
        size_t rebin_height = std::ceil((float)height / (float)rebin_xy);
        size_t rebin_nchan = std::ceil((float)nchan / (float)rebin_z);
        _cube_data.resize(casacore::IPosition(3, rebin_width, rebin_height, rebin_nchan));
        _cube_data = NAN;

        casacore::IPosition rebin_channel_shape(2, rebin_width, rebin_height);
        size_t rebin_channel_size = rebin_width * rebin_height;
        size_t new_chan(0);

        // Timer for progress updates
        auto t_start = std::chrono::high_resolution_clock::now();
        for (auto ichan = 0; ichan < nchan; ichan += rebin_z) {
            // Check for cancel
            if (_stop_cube) {
                cancel = true;
                _cube_data.resize();
                _stop_cube = false; // reset for next preview
                return;
            }

            // Check if can average next rebin_z channels
            if (ichan + rebin_z - 1 >= nchan) {
                break;
            }

            // Accumulate rebin_z channels
            std::vector<float> channel_sum(rebin_channel_size, 0.0);

            for (int rebin_chan = 0; rebin_chan < rebin_z; ++rebin_chan) {
                // Apply channel slicer to get data and mask for this channel
                casacore::Array<float> data;
                start(spectral_axis) = ichan + rebin_chan;
                casacore::Slicer channel_slicer(start, length);
                _preview_subimage.getSlice(data, channel_slicer, true);
                auto channel_data = data.tovector();

                if (rebin_xy > 1) {
                    // Rebin channel data in xy
                    std::vector<float> rebinned_data(rebin_channel_size, 0.0);
                    BlockSmooth(channel_data.data(), rebinned_data.data(), width, height, rebin_width, rebin_height, 0, 0, rebin_xy);

                    // Accumulate rebinned channel data
                    std::transform(channel_sum.begin(), channel_sum.end(), rebinned_data.begin(), channel_sum.begin(), std::plus<float>());
                } else {
                    // Accumulate channel data
                    std::transform(channel_sum.begin(), channel_sum.end(), channel_data.begin(), channel_sum.begin(), std::plus<float>());
                }
            }

            // Get mean for rebin_z
            std::transform(channel_sum.begin(), channel_sum.end(), channel_sum.begin(), [rebin_z](float& s) { return s / (float)rebin_z; });

            // Reshape vector to 2D and set cube data for this output channel
            casacore::Vector<float> channel_sumv(channel_sum);
            auto channel_cube_data = channel_sumv.reform(rebin_channel_shape);
            _cube_data[new_chan++] = channel_cube_data;

            // Update progress at interval
            float progress = (float)ichan / (float)nchan;
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            if ((dt > LOAD_DATA_PROGRESS_INTERVAL) || (progress >= 1.0)) {
                t_start = t_end;
                progress_callback(progress);
            }
        }

        spdlog::performance("PV preview cube data (rebin) loaded in {:.3f} ms", t.Elapsed().ms());
    } else {
        // No progress updates for each channel, but should be quick
        progress_callback(0.1);
        _cube_data = _preview_subimage.get(true); // no degenerate axis
        spdlog::performance("PV preview cube data (no rebin) loaded in {:.3f} ms", t.Elapsed().ms());
    }

    // Most of time spent loading data, calculating profiles is minimal
    progress_callback(1.0);
}

bool PvPreviewCube::CubeLoaded() {
    return !_cube_data.empty();
}

} // namespace carta
