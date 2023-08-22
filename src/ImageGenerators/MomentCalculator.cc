/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentCalculator.h"
#include "ImageGenerator.h"
#include "Logger/Logger.h"

#include <algorithm>

#define DOUBLE_NAN std::numeric_limits<double>::quiet_NaN()

using namespace carta;

MomentCalculator::MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image)
    : _image(image), _delta_velocity(DOUBLE_NAN), _do_include(false), _do_exclude(false), _cancelled(false) {
    _coord_sys = _image->coordinates();
    int spectral_axis = _coord_sys.spectralAxisNumber();

    if (spectral_axis > -1) {
        // Get velocity difference (dV)
        casacore::SpectralCoordinate spectral_coord = _coord_sys.spectralCoordinate();
        casacore::Quantum<casacore::Double> vel0;
        casacore::Quantum<casacore::Double> vel1;
        casacore::Double pix0 = spectral_coord.referencePixel()(0) - 0.5;
        casacore::Double pix1 = spectral_coord.referencePixel()(0) + 0.5;
        spectral_coord.pixelToVelocity(vel0, pix0);
        spectral_coord.pixelToVelocity(vel1, pix1);
        _delta_velocity = abs(vel1.getValue() - vel0.getValue());

        // Get velocities along the spectral axis
        auto image_shape = _image->shape();
        size_t spectral_axis_length = image_shape(spectral_axis);
        for (size_t chan = 0; chan < spectral_axis_length; ++chan) {
            casacore::Quantum<casacore::Double> vel;
            spectral_coord.pixelToVelocity(vel, chan);
            _velocities.push_back(vel.getValue());
        }
    }
}

void MomentCalculator::SetInExcludeRange(const std::vector<float>& include_pix, const std::vector<float>& exclude_pix) {
    if (!include_pix.empty() && !exclude_pix.empty()) {
        spdlog::error("[MomentCalculator] You can only give one of arguments for included or excluded pixel range!");
        return;
    }

    _pixel_range.resize(0);

    if (include_pix.size() == 1) {
        _pixel_range.resize(2);
        _pixel_range[0] = -std::abs(include_pix[0]);
        _pixel_range[1] = std::abs(include_pix[0]);
        _do_include = true;
    } else if (include_pix.size() == 2) {
        _pixel_range.resize(2);
        _pixel_range[0] = std::min(include_pix[0], include_pix[1]);
        _pixel_range[1] = std::max(include_pix[0], include_pix[1]);
        _do_include = true;
    }

    if (exclude_pix.size() == 1) {
        _pixel_range.resize(2);
        _pixel_range[0] = -std::abs(exclude_pix[0]);
        _pixel_range[1] = std::abs(exclude_pix[0]);
        _do_exclude = true;
    } else if (exclude_pix.size() == 2) {
        _pixel_range.resize(2);
        _pixel_range[0] = std::min(exclude_pix[0], exclude_pix[1]);
        _pixel_range[1] = std::max(exclude_pix[0], exclude_pix[1]);
        _do_exclude = true;
    }
}

void MomentCalculator::SetMomentTypes(const std::vector<int>& moment_types) {
    _moment_types = moment_types;
}

std::vector<std::shared_ptr<casacore::ImageInterface<float>>> MomentCalculator::CreateMoments(float* image_data, int moment_axis) {
    // Set moment image shapes
    casacore::CoordinateSystem coord_sys = _image->coordinates();
    casacore::IPosition in_shape = _image->shape();
    casacore::IPosition out_shape = in_shape;
    out_shape(moment_axis) = 1;

    // Get stokes axis, if any
    int stokes_axis = coord_sys.polarizationAxisNumber();
    if (stokes_axis > -1) {
        out_shape(stokes_axis) = 1;
    }

    // Get display axis lengths
    std::vector<int> display_axes;
    std::vector<size_t> display_sizes;
    for (int i = 0; i < out_shape.size(); ++i) {
        if (out_shape(i) > 1) {
            display_axes.emplace_back(i);
            display_sizes.emplace_back(out_shape(i));
        }
    }
    size_t depth = in_shape(moment_axis);

    // Set moment images
    int moments_size = _moment_types.size();
    std::vector<std::shared_ptr<casacore::ImageInterface<float>>> out_images(moments_size);

    if (display_sizes.size() != 2) {
        spdlog::error("Error on image moments calculation: can not get display axes.");
        return out_images;
    }

    // Initialize moment images data
    std::unordered_map<int, casacore::Array<float>> moment_data;
    std::unordered_map<int, casacore::Array<bool>> mask_data;
    for (auto type : _moment_types) {
        moment_data[type] = casacore::Array<float>(out_shape);
        mask_data[type] = casacore::Array<bool>(out_shape);
    }

    // Do calculations through the display plane
#pragma omp parallel for collapse(2)
    for (int y = 0; y < display_sizes[1]; ++y) {
        for (int x = 0; x < display_sizes[0]; ++x) {
            casacore::IPosition start_pos = casacore::IPosition(out_shape.size(), 0);
            start_pos(display_axes[0]) = x;
            start_pos(display_axes[1]) = y;
            DoCalculation(image_data, x, y, display_sizes[0], display_sizes[1], depth, start_pos, moment_data, mask_data);
        }
    }

    // Check is process cancelled
    if (_cancelled) {
        return out_images;
    }

    // Reset moment images
    for (int i = 0; i < moments_size; ++i) {
        std::shared_ptr<casacore::ImageInterface<float>>& image = out_images[i];
        image.reset(new casacore::TempImage<float>(out_shape, coord_sys));
        image->setUnits(_image->units());
        image->setMiscInfo(_image->miscInfo());
        image->setImageInfo(_image->imageInfo());
        image->makeMask("mask0", true, true);

        // Fill data into the moment image
        casacore::IPosition start_pos = casacore::IPosition(out_shape.size(), 0);
        image->putSlice(moment_data[_moment_types[i]], start_pos);

        // Fill mask data into the moment image
        if (image->isMasked()) {
            casacore::Lattice<casacore::Bool>& mask = image->pixelMask();
            if (mask.isWritable()) {
                mask.putSlice(mask_data[_moment_types[i]], start_pos);
            }
            mask.flush();
        }
        image->flush();
    }

    return out_images;
}

void MomentCalculator::DoCalculation(float* data, int x, int y, size_t width, size_t height, size_t depth,
    const casacore::IPosition& start_pos, std::unordered_map<int, casacore::Array<float>>& moment_data,
    std::unordered_map<int, casacore::Array<bool>>& mask_data) {
    double sum_i(0);
    double sum_iv(0);
    double sum_ivv(0);
    double sum_ii(0);
    double max = std::numeric_limits<double>::lowest();
    double min = std::numeric_limits<double>::max();
    double max_velocity, min_velocity;
    std::vector<float> intensities;
    size_t counts(0);

    auto valid_pixel_range = [&](const float& pixel) {
        if (!_do_include && !_do_exclude) {
            return true;
        }
        if (_do_include && (pixel >= _pixel_range[0] && pixel <= _pixel_range[1])) {
            return true;
        }
        if (_do_exclude && (pixel <= _pixel_range[0] || pixel >= _pixel_range[1])) {
            return true;
        }
        return false;
    };

    for (size_t z = 0; z < depth; ++z) {
        if (_cancelled) {
            break;
        }

        size_t idx = z * width * height + y * width + x;
        if (!std::isnan(data[idx]) && valid_pixel_range(data[idx])) {
            sum_i += data[idx];
            sum_iv += data[idx] * _velocities[z];
            sum_ivv += data[idx] * std::pow(_velocities[z], 2);
            sum_ii += std::pow(data[idx], 2);
            intensities.push_back(data[idx]);
            if (data[idx] > max) {
                max = data[idx];
                max_velocity = _velocities[z];
            }
            if (data[idx] < min) {
                min = data[idx];
                min_velocity = _velocities[z];
            }
            counts++;
        }
    }

    if (_cancelled) {
        return;
    }

    double mean_coordinate = std::abs(sum_i) > 0.0 ? sum_iv / sum_i : DOUBLE_NAN;
    double dispersion_coordinate =
        std::abs(sum_i) > 0.0 ? std::sqrt(std::abs((sum_ivv / sum_i) - std::pow(mean_coordinate, 2))) : DOUBLE_NAN;

    if (RequiredMomentType(0)) {
        moment_data[0](start_pos) = counts == 0 ? DOUBLE_NAN : sum_i / (double)counts;
        mask_data[0](start_pos) = counts != 0;
    }
    if (RequiredMomentType(1)) {
        moment_data[1](start_pos) = counts == 0 ? DOUBLE_NAN : _delta_velocity * sum_i;
        mask_data[1](start_pos) = counts != 0;
    }
    if (RequiredMomentType(2)) {
        moment_data[2](start_pos) = counts == 0 ? DOUBLE_NAN : mean_coordinate;
        mask_data[2](start_pos) = counts != 0 && !std::isnan(mean_coordinate);
    }
    if (RequiredMomentType(3)) {
        moment_data[3](start_pos) = counts == 0 ? DOUBLE_NAN : dispersion_coordinate;
        mask_data[3](start_pos) = counts != 0 && !std::isnan(dispersion_coordinate);
    }
    if (RequiredMomentType(4)) {
        moment_data[4](start_pos) = counts == 0 ? DOUBLE_NAN : FindMedian(intensities, depth);
        mask_data[4](start_pos) = counts != 0;
    }

    if (RequiredMomentType(6)) {
        double standard_deviation = counts == 0 ? DOUBLE_NAN : (sum_ii - sum_i * sum_i / counts) / (counts - 1);
        if (standard_deviation > 0) {
            standard_deviation = std::sqrt(standard_deviation);
        } else if (standard_deviation <= 0) {
            standard_deviation = DOUBLE_NAN;
        }
        moment_data[6](start_pos) = standard_deviation;
        mask_data[6](start_pos) = counts != 0;
    }

    if (RequiredMomentType(7)) {
        moment_data[7](start_pos) = counts == 0 ? DOUBLE_NAN : std::sqrt(sum_ii / counts);
        mask_data[7](start_pos) = counts != 0;
    }

    if (RequiredMomentType(8)) {
        double abs_mean_deviation = 0.0;
        if (counts) {
            double mean_i = sum_i / counts;
            for (auto intensity : intensities) {
                abs_mean_deviation += std::abs(intensity - mean_i);
            }
        }
        moment_data[8](start_pos) = counts == 0 ? DOUBLE_NAN : abs_mean_deviation / counts;
        mask_data[8](start_pos) = counts != 0;
    }

    if (RequiredMomentType(9)) {
        moment_data[9](start_pos) = counts == 0 ? DOUBLE_NAN : max;
        mask_data[9](start_pos) = counts != 0;
    }
    if (RequiredMomentType(10)) {
        moment_data[10](start_pos) = counts == 0 ? DOUBLE_NAN : max_velocity;
        mask_data[10](start_pos) = counts != 0;
    }
    if (RequiredMomentType(11)) {
        moment_data[11](start_pos) = counts == 0 ? DOUBLE_NAN : min;
        mask_data[11](start_pos) = counts != 0;
    }
    if (RequiredMomentType(12)) {
        moment_data[12](start_pos) = counts == 0 ? DOUBLE_NAN : min_velocity;
        mask_data[12](start_pos) = counts != 0;
    }
}

double MomentCalculator::FindMedian(std::vector<float>& array, size_t depth) {
    std::sort(array.begin(), array.end());

    // This strange algorithm is to be consistent with the original moment calculation for pixel median value
    size_t n = array.size();
    if (n % 2 == 0) {
        if (depth % 2 != 0) {
            return (array[(n - 1) / 2] + array[n / 2]) / 2;
        }
        return array[(n - 1) / 2];
    }
    return array[n / 2];
}

bool MomentCalculator::RequiredMomentType(int type) {
    return std::find(_moment_types.begin(), _moment_types.end(), type) != _moment_types.end();
}

void MomentCalculator::StopCalculation() {
    _cancelled = true;
}
