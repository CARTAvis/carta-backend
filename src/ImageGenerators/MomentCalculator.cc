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

MomentCalculator::MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, const std::vector<int>& moment_types)
    : _image(image), _delta_velocity(DOUBLE_NAN), _moment_types(moment_types) {
    _coord_sys = _image->coordinates();
    int spectral_axis = _coord_sys.spectralAxisNumber();

    if (spectral_axis > -1) {
        _spectral_coord = _coord_sys.spectralCoordinate();
        GetDeltaVelocity();

        auto image_shape = _image->shape();
        size_t spectral_axis_length = image_shape(spectral_axis);
        GetVelocities(spectral_axis_length);
    }
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
    for (auto type : _moment_types) {
        moment_data[type] = casacore::Array<float>(out_shape);
    }

    // Do calculations through the display plane
#pragma omp parallel for collapse(2)
    for (int y = 0; y < display_sizes[1]; ++y) {
        for (int x = 0; x < display_sizes[0]; ++x) {
            casacore::IPosition start_pos = casacore::IPosition(out_shape.size(), 0);
            start_pos(display_axes[0]) = x;
            start_pos(display_axes[1]) = y;
            DoCalculation(image_data, x, y, display_sizes[0], display_sizes[1], depth, start_pos, moment_data);
        }
    }

    // Reset moment images
    for (int i = 0; i < moments_size; ++i) {
        std::shared_ptr<casacore::ImageInterface<float>>& image = out_images[i];
        image.reset(new casacore::TempImage<float>(out_shape, coord_sys));
        image->setUnits(_image->units());
        image->setMiscInfo(_image->miscInfo());
        image->setImageInfo(_image->imageInfo());
        // image->makeMask("mask0", true, true);

        // Fill data into the moment image
        casacore::IPosition start_pos = casacore::IPosition(out_shape.size(), 0);
        image->putSlice(moment_data[_moment_types[i]], start_pos);
        image->flush();
    }

    return out_images;
}

void MomentCalculator::DoCalculation(float* data, int x, int y, size_t width, size_t height, size_t depth,
    const casacore::IPosition& start_pos, std::unordered_map<int, casacore::Array<float>>& moment_data) {
    double sum_i(0);
    double sum_iv(0);
    double sum_ivv(0);
    double sum_ii(0);
    double max = std::numeric_limits<double>::lowest();
    double min = std::numeric_limits<double>::max();
    double max_velocity, min_velocity;
    std::vector<float> intensities;
    size_t counts(0);
    for (size_t z = 0; z < depth; ++z) {
        size_t idx = z * width * height + y * width + x;
        if (!std::isnan(data[idx])) {
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

    double mean_coordinate = sum_iv / sum_i;
    double dispersion_coordinate = std::sqrt(std::abs(sum_ivv / sum_i - std::pow(mean_coordinate, 2)));

    if (RequiredMomentType(0)) {
        moment_data[0](start_pos) = counts == 0 ? DOUBLE_NAN : sum_i / (double)counts;
    }
    if (RequiredMomentType(1)) {
        moment_data[1](start_pos) = counts == 0 ? DOUBLE_NAN : _delta_velocity * sum_i;
    }
    if (RequiredMomentType(2)) {
        moment_data[2](start_pos) = counts == 0 ? DOUBLE_NAN : mean_coordinate;
    }
    if (RequiredMomentType(3)) {
        moment_data[3](start_pos) = counts == 0 ? DOUBLE_NAN : dispersion_coordinate;
    }
    if (RequiredMomentType(4)) {
        moment_data[4](start_pos) = counts == 0 ? DOUBLE_NAN : FindMedian(intensities, depth);
    }

    if (RequiredMomentType(6)) {
        double standard_deviation = counts == 0 ? DOUBLE_NAN : (sum_ii - sum_i * sum_i / counts) / (counts - 1);
        if (standard_deviation > 0) {
            standard_deviation = std::sqrt(standard_deviation);
        } else if (standard_deviation <= 0) {
            standard_deviation = 0.0;
        }
        moment_data[6](start_pos) = standard_deviation;
    }

    if (RequiredMomentType(7)) {
        moment_data[7](start_pos) = counts == 0 ? DOUBLE_NAN : std::sqrt(sum_ii / counts);
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
    }

    if (RequiredMomentType(9)) {
        moment_data[9](start_pos) = counts == 0 ? DOUBLE_NAN : max;
    }
    if (RequiredMomentType(10)) {
        moment_data[10](start_pos) = counts == 0 ? DOUBLE_NAN : max_velocity;
    }
    if (RequiredMomentType(11)) {
        moment_data[11](start_pos) = counts == 0 ? DOUBLE_NAN : min;
    }
    if (RequiredMomentType(12)) {
        moment_data[12](start_pos) = counts == 0 ? DOUBLE_NAN : min_velocity;
    }
}

void MomentCalculator::GetDeltaVelocity() {
    casacore::Quantum<casacore::Double> vel0;
    casacore::Quantum<casacore::Double> vel1;
    casacore::Double pix0 = _spectral_coord.referencePixel()(0) - 0.5;
    casacore::Double pix1 = _spectral_coord.referencePixel()(0) + 0.5;
    _spectral_coord.pixelToVelocity(vel0, pix0);
    _spectral_coord.pixelToVelocity(vel1, pix1);
    _delta_velocity = abs(vel1.getValue() - vel0.getValue());
}

void MomentCalculator::GetVelocities(size_t spectral_axis_length) {
    for (size_t chan = 0; chan < spectral_axis_length; ++chan) {
        casacore::Quantum<casacore::Double> vel;
        _spectral_coord.pixelToVelocity(vel, chan);
        _velocities.push_back(vel.getValue());
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
