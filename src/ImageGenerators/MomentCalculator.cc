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
    : _image(image), _moment_types(moment_types) {
    _coord_sys = _image->coordinates();
    _spectral_coord = _coord_sys.spectralCoordinate();
}

std::vector<std::shared_ptr<casacore::ImageInterface<float>>> MomentCalculator::CreateMoments(
    float* image_data, int moment_axis, int stokes) {
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
    std::vector<int> display_lengths;
    for (int i = 0; i < out_shape.size(); ++i) {
        if (out_shape(i) > 1) {
            display_axes.emplace_back(i);
            display_lengths.emplace_back(out_shape(i));
        }
    }

    // Set moment images
    int moments_size = _moment_types.size();
    std::vector<std::shared_ptr<casacore::ImageInterface<float>>> out_images(moments_size);

    if (display_lengths.size() != 2) {
        spdlog::error("Error on image moments calculation: can not get display axes.");
        return out_images;
    }

    // Initialize moment images data
    std::unordered_map<int, casacore::Array<float>> moment_data;
    for (auto type : _moment_types) {
        moment_data[type] = casacore::Array<float>(out_shape);
    }

    // Do calculations through the display plane
    for (int y = 0; y < display_lengths[1]; ++y) {
        for (int x = 0; x < display_lengths[0]; ++x) {
            // Get z-axis data at pixel coordinate (x, y)
            std::vector<float> spectral_data;
            for (int z = 0; z < in_shape(moment_axis); ++z) {
                int idx = display_lengths[0] * display_lengths[1] * z + display_lengths[0] * y + x;
                spectral_data.push_back(image_data[idx]);
            }

            // Do calculations
            std::unordered_map<int, float> results;
            DoCalculation(spectral_data.data(), spectral_data.size(), results);

            // Fill calculation results
            for (auto type : _moment_types) {
                casacore::IPosition start_pos = casacore::IPosition(out_shape.size(), 0);
                start_pos(display_axes[0]) = x;
                start_pos(display_axes[1]) = y;
                moment_data[type](start_pos) = results[type];
            }
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

void MomentCalculator::DoCalculation(float* data, size_t length, std::unordered_map<int, float>& results) {
    double sum_i(0);
    double sum_iv(0);
    double sum_ivv(0);
    double sum_ii(0);
    double max = std::numeric_limits<double>::lowest();
    double min = std::numeric_limits<double>::max();
    double max_velocity, min_velocity;
    std::vector<float> intensities;
    size_t counts(0);
    for (size_t i = 0; i < length; ++i) {
        if (!std::isnan(data[i])) {
            double velocity = GetVelocity(i);
            sum_i += data[i];
            sum_iv += data[i] * velocity;
            sum_ivv += data[i] * std::pow(velocity, 2);
            sum_ii += std::pow(data[i], 2);
            intensities.push_back(data[i]);
            if (data[i] > max) {
                max = data[i];
                max_velocity = velocity;
            }
            if (data[i] < min) {
                min = data[i];
                min_velocity = velocity;
            }
            counts++;
        }
    }

    double mean_coordinate = sum_iv / sum_i;
    double dispersion_coordinate = std::sqrt(std::abs(sum_ivv / sum_i - std::pow(mean_coordinate, 2)));

    if (RequiredMomentType(0)) {
        results[0] = counts == 0 ? DOUBLE_NAN : sum_i / (double)counts;
    }
    if (RequiredMomentType(1)) {
        results[1] = counts == 0 ? DOUBLE_NAN : GetDeltaVelocity() * sum_i;
    }
    if (RequiredMomentType(2)) {
        results[2] = counts == 0 ? DOUBLE_NAN : mean_coordinate;
    }
    if (RequiredMomentType(3)) {
        results[3] = counts == 0 ? DOUBLE_NAN : dispersion_coordinate;
    }
    if (RequiredMomentType(4)) {
        results[4] = counts == 0 ? DOUBLE_NAN : FindMedian(intensities);
    }

    if (RequiredMomentType(6)) {
        double standard_deviation = counts == 0 ? DOUBLE_NAN : (sum_ii - sum_i * sum_i / counts) / (counts - 1);
        if (standard_deviation > 0) {
            standard_deviation = std::sqrt(standard_deviation);
        } else if (standard_deviation <= 0) {
            standard_deviation = 0.0;
        }
        results[6] = standard_deviation;
    }

    if (RequiredMomentType(7)) {
        results[7] = counts == 0 ? DOUBLE_NAN : std::sqrt(sum_ii / counts);
    }

    if (RequiredMomentType(8)) {
        double abs_mean_deviation = 0.0;
        if (counts) {
            double mean_i = sum_i / counts;
            for (auto intensity : intensities) {
                abs_mean_deviation += std::abs(intensity - mean_i);
            }
        }
        results[8] = counts == 0 ? DOUBLE_NAN : abs_mean_deviation / counts;
    }

    if (RequiredMomentType(9)) {
        results[9] = counts == 0 ? DOUBLE_NAN : max;
    }
    if (RequiredMomentType(10)) {
        results[10] = counts == 0 ? DOUBLE_NAN : max_velocity;
    }
    if (RequiredMomentType(11)) {
        results[11] = counts == 0 ? DOUBLE_NAN : min;
    }
    if (RequiredMomentType(12)) {
        results[12] = counts == 0 ? DOUBLE_NAN : min_velocity;
    }
}

double MomentCalculator::GetDeltaVelocity() {
    casacore::Quantum<casacore::Double> vel0;
    casacore::Quantum<casacore::Double> vel1;
    casacore::Double pix0 = _spectral_coord.referencePixel()(0) - 0.5;
    casacore::Double pix1 = _spectral_coord.referencePixel()(0) + 0.5;
    _spectral_coord.pixelToVelocity(vel0, pix0);
    _spectral_coord.pixelToVelocity(vel1, pix1);
    return abs(vel1.getValue() - vel0.getValue());
}

double MomentCalculator::GetVelocity(double chan) {
    casacore::Quantum<casacore::Double> vel;
    _spectral_coord.pixelToVelocity(vel, chan);
    return vel.getValue();
}

double MomentCalculator::FindMedian(std::vector<float>& array) {
    size_t n = array.size();
    if (n % 2 == 0) {
        nth_element(array.begin(), array.begin() + n / 2, array.end());
        nth_element(array.begin(), array.begin() + (n - 1) / 2, array.end());
        return (double)(array[(n - 1) / 2] + array[n / 2]) / 2.0;
    }
    nth_element(array.begin(), array.begin() + n / 2, array.end());
    return (double)array[n / 2];
}

bool MomentCalculator::RequiredMomentType(int type) {
    return std::find(_moment_types.begin(), _moment_types.end(), type) != _moment_types.end();
}
