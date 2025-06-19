/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define SIGMA_TO_FWHM std::sqrt(8.0 * std::log(2.0))

#include "InitialValueCalculator.h"

using namespace carta;

InitialValueCalculator::InitialValueCalculator(FitData* fit_data, float image_std, std::string image_unit)
    : _image(fit_data->data),
      _width(fit_data->width),
      _height(fit_data->n / fit_data->width),
      _offset_x(fit_data->offset_x),
      _offset_y(fit_data->offset_y),
      _image_std(image_std),
      _image_unit(image_unit.empty() ? "arbitrary" : image_unit) {}

bool InitialValueCalculator::CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values, std::string& log) {
    size_t request_num_components = initial_values.size();
    initial_values.clear();

    for (float i = 4.0; i >= 0.0; i -= 1.0) {
        std::vector<int> centroid_indexes;
        if (request_num_components == 1) {
            centroid_indexes = {0};
        } else {
            spdlog::debug("Generating centroids using KMeans++ with threshold of {} * MAD = {}", i, _image_std * i);
            centroid_indexes = KMeansPlusPlus(request_num_components, _image_std * i);
        }

        if (centroid_indexes.size() < request_num_components && i > 0.0) {
            spdlog::debug("Generated {} centroid(s) instead of {}.", centroid_indexes.size(), request_num_components);
            continue;
        }

        spdlog::debug("Generating initial values using method of moments for {} component(s).", centroid_indexes.size());
        std::vector<GaussianParams> estimated_components_tmp = MethodOfMoments(centroid_indexes);
        std::vector<double> center_x_tmp(estimated_components_tmp.size(), 0.0);
        std::vector<double> center_y_tmp(estimated_components_tmp.size(), 0.0);
        std::vector<double> radius_tmp(estimated_components_tmp.size(), 0.0);
        for (size_t i = 0; i < estimated_components_tmp.size(); ++i) {
            GaussianParams params = estimated_components_tmp[i];
            center_x_tmp[i] = params.center_x;
            center_y_tmp[i] = params.center_y;
            radius_tmp[i] = std::max(params.fwhm_x, params.fwhm_y);
        }
        std::vector<GaussianParams> estimated_components = MethodOfMoments(centroid_indexes, true, center_x_tmp, center_y_tmp, radius_tmp);

        initial_values.clear();
        for (size_t i = 0; i < estimated_components.size(); ++i) {
            GaussianParams params = estimated_components[i];
            double center_x = params.center_x;
            double center_y = params.center_y;
            double amp = params.amp;
            double fwhm_x = params.fwhm_x;
            double fwhm_y = params.fwhm_y;
            double pa = params.pa;

            if (std::isnan(center_x) || std::isnan(center_y) || std::isnan(amp) || std::isnan(fwhm_x) || std::isnan(fwhm_y) ||
                std::isnan(pa)) {
                spdlog::debug(
                    "Invalid initial value for component {}: ({}, {}, {}, {}, {}, {})", i, center_x, center_y, amp, fwhm_x, fwhm_y, pa);
                continue;
            }

            if (fwhm_x > std::max(_width, _height) * 2 || fwhm_y > std::max(_width, _height) * 2) {
                spdlog::debug("FWHM too large for component {}: ({}, {}, {}, {}, {}, {})", i, center_x, center_y, amp, fwhm_x, fwhm_y, pa);
                continue;
            }

            if (center_x < -std::max(fwhm_x, fwhm_y) / 4 || center_x > _width + std::max(fwhm_x, fwhm_y) / 4 ||
                center_y < -std::max(fwhm_x, fwhm_y) / 4 || center_y > _height + std::max(fwhm_x, fwhm_y) / 4) {
                spdlog::debug("Center too far from the image boundary for component {}: ({}, {}, {}, {}, {}, {})", i, center_x, center_y,
                    amp, fwhm_x, fwhm_y, pa);
                continue;
            }

            initial_values.push_back(GetGaussianComponent(params));
        }

        if (initial_values.size() == request_num_components) {
            break;
        }

        spdlog::debug("Generated initial values of {} component(s) instead of {}.", initial_values.size(), request_num_components);
    }

    if (initial_values.empty()) {
        spdlog::debug("No valid initial values generated, setting default values.");
        double center_x = _width / 2 + _offset_x;
        double center_y = _height / 2 + _offset_y;
        double fwhm = std::min(_width, _height) / 2;
        initial_values.push_back(GetGaussianComponent(GaussianParams(center_x, center_y, 1.0, fwhm, fwhm, 0.0)));
    }

    log = GetLog(initial_values);

    return true;
}

std::vector<GaussianParams> InitialValueCalculator::MethodOfMoments(std::vector<int> centroid_indexes, bool apply_filter,
    std::vector<double> center_x, std::vector<double> center_y, std::vector<double> radius) {
    std::vector<GaussianParams> result;
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

        result.push_back(GaussianParams(mx[k], my[k], amp, fwhm_x, fwhm_y, pa));
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

std::string InitialValueCalculator::GetLog(std::vector<CARTA::GaussianComponent>& initial_values) {
    std::string log = fmt::format("Generated initial values of {} component(s)\n", initial_values.size());
    for (size_t i = 0; i < initial_values.size(); i++) {
        CARTA::GaussianComponent component = initial_values[i];
        log += fmt::format("Component #{}:\n", i + 1);

        log += fmt::format("Center X        = {:6f} (px)\n", component.center().x());
        log += fmt::format("Center Y        = {:6f} (px)\n", component.center().y());
        log += fmt::format("Amplitude       = {:6f} ({})\n", component.amp(), _image_unit);
        log += fmt::format("FWHM Major Axis = {:6f} (px)\n", component.fwhm().x());
        log += fmt::format("FWHM Minor Axis = {:6f} (px)\n", component.fwhm().y());
        log += fmt::format("P.A.            = {:6f} (deg)\n", component.pa());
        log += "\n";
    }

    return log;
}
