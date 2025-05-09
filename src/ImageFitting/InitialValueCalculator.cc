/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define SIGMA_TO_FWHM std::sqrt(8.0 * std::log(2.0))

#include "InitialValueCalculator.h"

using namespace carta;

InitialValueCalculator::InitialValueCalculator(float* image, size_t width, size_t height, size_t offset_x, size_t offset_y) {
    _image = image;
    _width = width;
    _height = height;
    _offset_x = offset_x;
    _offset_y = offset_y;
}

bool InitialValueCalculator::CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values, float image_std) {
    size_t num_components = initial_values.size();

    if (num_components == 1) {
        auto [center_x_tmp, center_y_tmp, amp_tmp, fwhm_x_tmp, fwhm_y_tmp, pa_tmp] = MethodOfMoments();
        auto [center_x, center_y, amp, fwhm_x, fwhm_y, pa] =
            MethodOfMoments(true, center_x_tmp, center_y_tmp, std::max(fwhm_x_tmp, fwhm_y_tmp));

        auto center = Message::DoublePoint(center_x + _offset_x, center_y + _offset_y);
        auto fwhm = Message::DoublePoint(fwhm_x, fwhm_y);
        auto component = Message::GaussianComponent(center, amp, fwhm, pa);
        initial_values.clear();
        initial_values.push_back(component);

        return true;
    }

    // TODO: support multi components
    std::vector<int> centroid_indexes = KMeansPlusPlus(num_components, image_std * 4.0);

    initial_values.clear();
    for (size_t i = 0; i < num_components; ++i) {
        auto center = Message::DoublePoint(centroid_indexes[i] % _width + _offset_x, centroid_indexes[i] / _width + _offset_y);
        auto fwhm = Message::DoublePoint(10, 10);
        auto component = Message::GaussianComponent(center, 1, fwhm, 0);

        initial_values.push_back(component);
    }

    return true;
}

std::tuple<double, double, double, double, double, double> InitialValueCalculator::MethodOfMoments(
    bool apply_filter, double center_x, double center_y, double radius) {
    double m0 = 0.0, mx = 0.0, my = 0.0, mxx = 0.0, myy = 0.0, mxy = 0.0;

    for (int j = 0; j < _height; ++j) {
        for (int i = 0; i < _width; ++i) {
            if (!apply_filter || (std::sqrt(std::pow(i - center_x, 2.0) + std::pow(j - center_y, 2.0)) <= radius)) {
                int index = j * _width + i;
                double value = _image[index];

                if (!std::isnan(value)) {
                    m0 += value;
                    mx += i * value;
                    my += j * value;
                    mxx += i * i * value;
                    myy += j * j * value;
                    mxy += i * j * value;
                }
            }
        }
    }

    mx /= m0;
    my /= m0;
    mxx = mxx / m0 - mx * mx;
    myy = myy / m0 - my * my;
    mxy = mxy / m0 - mx * my;

    double amp = m0 * 0.5 * std::pow(std::abs(mxx * myy - mxy * mxy), -0.5) / M_PI;
    double tmp = std::sqrt(std::pow(mxx - myy, 2.0) + 4.0 * mxy * mxy);
    double fwhm_x = std::sqrt(0.5 * (std::abs(mxx + myy + tmp))) * SIGMA_TO_FWHM;
    double fwhm_y = std::sqrt(0.5 * (std::abs(mxx + myy - tmp))) * SIGMA_TO_FWHM;
    double pa = -0.5 * std::atan2(2.0 * mxy, myy - mxx) * 180.0 / M_PI;

    return {mx, my, amp, fwhm_x, fwhm_y, pa};
}

std::vector<int> InitialValueCalculator::KMeansPlusPlus(size_t num_components, float threshold) {
    size_t size = _width * _height;
    size_t trial_num = 2 + std::floor(std::log(num_components));

    std::vector<int> centroid_indexes;
    centroid_indexes.reserve(num_components);

    float first_centroid_index = rand() % size;
    // Generate a random number between 0 and the total sum of the weights
    double sum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(_image[i]) < threshold) {
            continue;
        }
        sum += std::abs(_image[i]);
    }
    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(0.0, sum);
    double random_value = distribution(generator);
    // Find the index where the random number falls in the cumulative distribution
    sum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(_image[i]) < threshold) {
            continue;
        }
        sum += std::abs(_image[i]);
        if (random_value <= sum) {
            first_centroid_index = i;
            break;
        }
    }
    centroid_indexes.push_back(first_centroid_index);

    float current_potential = 0.0;
    for (int i = 0; i < size; ++i) {
        if (std::abs(_image[i]) < threshold) {
            continue;
        }
        float distance =
            std::pow(i % _width - centroid_indexes[0] % _width, 2.0) + std::pow(i / _width - centroid_indexes[0] / _width, 2.0);
        current_potential += std::abs(_image[i]) * distance;
    }

    for (size_t k = 1; k < num_components; ++k) {
        float next_centroid_index = rand() % size;
        float min_potential = std::numeric_limits<float>::max();

        for (size_t t = 0; t < trial_num; ++t) {
            int centroid_index_candidate = 0;

            // Generate a random number between 0 and the weighted potential
            std::uniform_real_distribution<float> distribution(0.0, current_potential);
            random_value = distribution(generator);
            // Find the index where the random number falls in the cumulative distribution
            float sum = 0.0;
            for (int i = 0; i < size; ++i) {
                if (std::abs(_image[i]) < threshold) {
                    continue;
                }

                float min_distance = std::numeric_limits<float>::max();
                for (size_t j = 0; j < centroid_indexes.size(); ++j) {
                    float distance =
                        std::pow(i % _width - centroid_indexes[j] % _width, 2.0) + std::pow(i / _width - centroid_indexes[j] / _width, 2.0);
                    if (distance < min_distance) {
                        min_distance = distance;
                    }
                }
                sum += std::abs(_image[i]) * min_distance;
                if (random_value <= sum) {
                    centroid_index_candidate = i;
                    break;
                }
            }

            float candidate_potential = 0.0;
            for (int i = 0; i < size; ++i) {
                if (std::abs(_image[i]) < threshold) {
                    continue;
                }

                float min_distance = std::numeric_limits<float>::max();
                for (size_t j = 0; j < centroid_indexes.size(); ++j) {
                    float distance =
                        std::pow(i % _width - centroid_indexes[j] % _width, 2.0) + std::pow(i / _width - centroid_indexes[j] / _width, 2.0);
                    if (distance < min_distance) {
                        min_distance = distance;
                    }
                }
                float distance_to_candidate = std::pow(i % _width - centroid_index_candidate % _width, 2.0) +
                                              std::pow(i / _width - centroid_index_candidate / _width, 2.0);
                if (distance_to_candidate < min_distance) {
                    min_distance = distance_to_candidate;
                }

                candidate_potential += std::abs(_image[i]) * min_distance;
            }

            if (candidate_potential < min_potential) {
                min_potential = candidate_potential;
                next_centroid_index = centroid_index_candidate;
            }
        }

        current_potential = min_potential;
        centroid_indexes.push_back(next_centroid_index);
    }

    spdlog::debug("Generated {} centroids.", centroid_indexes.size());
    for (size_t i = 0; i < centroid_indexes.size(); ++i) {
        spdlog::debug("Centroid #{}: ({}, {})", i, centroid_indexes[i] % _width, centroid_indexes[i] / _width);
    }

    return centroid_indexes;
}

std::string InitialValueCalculator::GetLog(std::vector<CARTA::GaussianComponent>& initial_values, std::string image_unit) {
    if (image_unit.empty()) {
        image_unit = "arbitrary";
    }

    std::string log = fmt::format("Generated initial values of {} component(s)\n", initial_values.size());
    for (size_t i = 0; i < initial_values.size(); i++) {
        CARTA::GaussianComponent component = initial_values[i];
        log += fmt::format("Component #{}:\n", i + 1);

        log += fmt::format("Center X        = {:6f} (px)\n", component.center().x());
        log += fmt::format("Center Y        = {:6f} (px)\n", component.center().y());
        log += fmt::format("Amplitude       = {:6f} ({})\n", component.amp(), image_unit);
        log += fmt::format("FWHM Major Axis = {:6f} (px)\n", component.fwhm().x());
        log += fmt::format("FWHM Minor Axis = {:6f} (px)\n", component.fwhm().y());
        log += fmt::format("P.A.            = {:6f} (deg)\n", component.pa());
        log += "\n";
    }

    return log;
}
