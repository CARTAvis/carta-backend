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

bool InitialValueCalculator::CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values) {
    if (initial_values.size() == 1) {
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

    return false;
}

std::tuple<double, double, double, double, double, double> InitialValueCalculator::MethodOfMoments(
    bool apply_filter, double center_x, double center_y, double radius) {
    double m0 = 0.0, mx = 0.0, my = 0.0, mxx = 0.0, myy = 0.0, mxy = 0.0;

    for (int j = 0; j < _height; ++j) {
        for (int i = 0; i < _width; ++i) {
            if (!apply_filter || (std::sqrt(std::pow(i - center_x, 2.0) + std::pow(j - center_y, 2.0)) <= radius)) {
                int index = j * _width + i;
                double value = _image[index];

                if (!isnan(value)) {
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
