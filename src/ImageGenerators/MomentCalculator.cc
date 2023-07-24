/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentCalculator.h"
#include "ImageGenerator.h"
#include "Logger/Logger.h"

#include <algorithm>

using namespace carta;

MomentCalculator::MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, const std::vector<int>& moment_types)
    : _image(image), _moment_types(moment_types) {
    _coord_sys = _image->coordinates();
    _spectral_coord = _coord_sys.spectralCoordinate();
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
    double standard_deviation = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : (sum_ii - sum_i * sum_i / counts) / (counts - 1);
    if (standard_deviation > 0) {
        standard_deviation = std::sqrt(standard_deviation);
    } else if (standard_deviation <= 0) {
        standard_deviation = 0.0;
    }
    double abs_mean_deviation = 0.0;
    if (counts) {
        double mean_i = sum_i / counts;
        for (auto intensity : intensities) {
            abs_mean_deviation += std::abs(intensity - mean_i);
        }
    }

    results[0] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : sum_i / (double)counts;
    results[1] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : GetDeltaVelocity() * sum_i;
    results[2] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : mean_coordinate;
    results[3] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : dispersion_coordinate;
    results[4] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : FindMedian(intensities);
    // results[5]: median coordinate is not available so far
    results[6] = standard_deviation;
    results[7] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(sum_ii / counts);
    results[8] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : abs_mean_deviation / counts;
    results[9] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : max;
    results[10] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : max_velocity;
    results[11] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : min;
    results[12] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : min_velocity;
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
