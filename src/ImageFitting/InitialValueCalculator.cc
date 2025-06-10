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

    std::vector<int> centroid_indexes;
    if (num_components == 1) {
        centroid_indexes = {0};
    } else {
        for (float i = 4.0; i >= 0.0; i -= 1.0) {
            spdlog::debug("Generating centroids using KMeans++ with threshold of {} * MAD = {}", i, image_std * i);
            centroid_indexes = KMeansPlusPlus(num_components, image_std * i);
            if (centroid_indexes.size() == num_components) {
                break;
            }
        }

        if (centroid_indexes.size() == 0) {
            spdlog::debug("Failed to generate centroids using KMeans++.");
            return false;
        }

        if (centroid_indexes.size() < num_components) {
            spdlog::debug("Generated {} centroids instead of {}.", centroid_indexes.size(), num_components);
        }
    }

    std::vector<double> center_x_tmp(num_components, 0.0);
    std::vector<double> center_y_tmp(num_components, 0.0);
    std::vector<double> radius_tmp(num_components, 0.0);
    std::vector<std::tuple<double, double, double, double, double, double>> estimated_components_tmp = MethodOfMoments(centroid_indexes);
    for (size_t i = 0; i < num_components; ++i) {
        auto [center_x, center_y, amp, fwhm_x, fwhm_y, pa] = estimated_components_tmp[i];
        center_x_tmp[i] = center_x;
        center_y_tmp[i] = center_y;
        radius_tmp[i] = std::max(fwhm_x, fwhm_y);
    }
    std::vector<std::tuple<double, double, double, double, double, double>> estimated_components =
        MethodOfMoments(centroid_indexes, true, center_x_tmp, center_y_tmp, radius_tmp);

    initial_values.clear();
    for (size_t i = 0; i < num_components; ++i) {
        auto [center_x, center_y, amp, fwhm_x, fwhm_y, pa] = estimated_components[i];
        auto center = Message::DoublePoint(center_x + _offset_x, center_y + _offset_y);
        auto fwhm = Message::DoublePoint(fwhm_x, fwhm_y);
        auto component = Message::GaussianComponent(center, amp, fwhm, pa);
        initial_values.push_back(component);
    }

    return true;
}

std::vector<std::tuple<double, double, double, double, double, double>> InitialValueCalculator::MethodOfMoments(
    std::vector<int> centroid_indexes, bool apply_filter, std::vector<double> center_x, std::vector<double> center_y,
    std::vector<double> radius) {
    std::vector<std::tuple<double, double, double, double, double, double>> result;
    std::vector<double> m0(centroid_indexes.size(), 0.0);
    std::vector<double> mx(centroid_indexes.size(), 0.0);
    std::vector<double> my(centroid_indexes.size(), 0.0);
    std::vector<double> mxx(centroid_indexes.size(), 0.0);
    std::vector<double> myy(centroid_indexes.size(), 0.0);
    std::vector<double> mxy(centroid_indexes.size(), 0.0);

    for (int j = 0; j < _height; ++j) {
        for (int i = 0; i < _width; ++i) {
            int closest_centroid_index = -1;
            if (centroid_indexes.size() == 1) {
                closest_centroid_index = 0;
            } else {
                double min_distance = std::numeric_limits<double>::max();
                for (size_t k = 0; k < centroid_indexes.size(); ++k) {
                    double distance =
                        std::sqrt(std::pow(i - centroid_indexes[k] % _width, 2.0) + std::pow(j - centroid_indexes[k] / _width, 2.0));
                    if (distance < min_distance) {
                        min_distance = distance;
                        closest_centroid_index = k;
                    }
                }
            }

            if (closest_centroid_index != -1 &&
                (!apply_filter || (std::sqrt(std::pow(i - center_x[closest_centroid_index], 2.0) +
                                             std::pow(j - center_y[closest_centroid_index], 2.0)) <= radius[closest_centroid_index]))) {
                int index = j * _width + i;
                double value = _image[index];

                if (!std::isnan(value)) {
                    m0[closest_centroid_index] += value;
                    mx[closest_centroid_index] += i * value;
                    my[closest_centroid_index] += j * value;
                    mxx[closest_centroid_index] += i * i * value;
                    myy[closest_centroid_index] += j * j * value;
                    mxy[closest_centroid_index] += i * j * value;
                }
            }
        }
    }

    for (size_t k = 0; k < centroid_indexes.size(); ++k) {
        mx[k] /= m0[k];
        my[k] /= m0[k];
        mxx[k] = mxx[k] / m0[k] - mx[k] * mx[k];
        myy[k] = myy[k] / m0[k] - my[k] * my[k];
        mxy[k] = mxy[k] / m0[k] - mx[k] * my[k];

        double amp = m0[k] * 0.5 * std::pow(std::abs(mxx[k] * myy[k] - mxy[k] * mxy[k]), -0.5) / M_PI;
        double tmp = std::sqrt(std::pow(mxx[k] - myy[k], 2.0) + 4.0 * mxy[k] * mxy[k]);
        double fwhm_x = std::sqrt(0.5 * (std::abs(mxx[k] + myy[k] + tmp))) * SIGMA_TO_FWHM;
        double fwhm_y = std::sqrt(0.5 * (std::abs(mxx[k] + myy[k] - tmp))) * SIGMA_TO_FWHM;
        double pa = -0.5 * std::atan2(2.0 * mxy[k], myy[k] - mxx[k]) * 180.0 / M_PI;

        result.push_back({mx[k], my[k], amp, fwhm_x, fwhm_y, pa});
    }

    return result;
}

std::vector<int> InitialValueCalculator::KMeansPlusPlus(size_t num_components, float threshold) {
    size_t size = _width * _height;
    size_t trial_num = 2 + std::floor(std::log(num_components));

    std::vector<int> centroid_indexes;
    centroid_indexes.reserve(num_components);

    // Select the first centroid randomly, weighted by the the absolute values
    // Initialize the first centroid index randomly
    float first_centroid_index = rand() % size;

    // Calculate the sum of the absolute values
    double sum = 0.0;
    int sample_num = 0;
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(_image[i]) < threshold) {
            continue;
        }
        sum += std::abs(_image[i]);
        sample_num++;
    }

    // Return empty vector if there are not enough data points
    spdlog::debug("Total data points: {}", sample_num);
    if (sample_num < num_components) {
        spdlog::debug("Insufficient data points with the given threshold.");
        return centroid_indexes;
    }

    // Generate a random number between 0 and the sum of the absolute values
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

    // Select the next centroid randomly, weighted by the weighted potential
    // Calculate the sum of the weighted potential
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
        // Initialize the next centroid index randomly
        float next_centroid_index = rand() % size;

        // Try for a number of trials to find the next centroid index
        float min_potential = std::numeric_limits<float>::max();
        for (size_t t = 0; t < trial_num; ++t) {
            int centroid_index_candidate = 0;

            // Generate a random number between 0 and the sum of the weighted potential
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

            // Calculate the sum of the weighted potential
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

            // If the candidate potential is less than the minimum potential, update the next centroid index
            if (candidate_potential < min_potential) {
                min_potential = candidate_potential;
                next_centroid_index = centroid_index_candidate;
            }
        }

        current_potential = min_potential;
        centroid_indexes.push_back(next_centroid_index);
    }

    // Remove duplicate centroid indexes
    std::sort(centroid_indexes.begin(), centroid_indexes.end());
    centroid_indexes.erase(std::unique(centroid_indexes.begin(), centroid_indexes.end()), centroid_indexes.end());

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
