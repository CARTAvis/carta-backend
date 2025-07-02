/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTING_UTIL_H_
#define CARTA_SRC_IMAGEFITTING_UTIL_H_

#include <vector>

#include <carta-protobuf/fitting_request.pb.h>

#include "Util/Message.h"

namespace carta {

/** @brief Data structure for storing fitting-related data. */
struct FitData {
    /** @brief Pointer to the image data. */
    float* data;
    /** @brief The width of the image. */
    size_t width;
    /** @brief Number of pixels. */
    size_t n;
    /** @brief Number of pixels excluding nan pixels. */
    size_t n_notnan;
    /** @brief X-axis offset from the fitting region to the entire image. */
    size_t offset_x;
    /** @brief Y-axis offset from the fitting region to the entire image. */
    size_t offset_y;
    /** @brief Indexes of the Gaussian parameters in the fittig parameters. */
    std::vector<int> fit_values_indexes;
    /** @brief Initial values of the unfixed parameters. */
    std::vector<double> initial_values;
    /** @brief Whether to stop the fitting process. */
    bool stop_fitting;
};

/** @brief Data structure for storing parameters of a Gaussian component. */
struct GaussianParams {
    /** @brief X-coordinate of the center in pixels. */
    double center_x;
    /** @brief Y-coordinate of the center in pixels. */
    double center_y;
    /** @brief Amplitude of the component. */
    double amp;
    /** @brief Full width at half maximum along x-coordinate in pixels. */
    double fwhm_x;
    /** @brief Full width at half maximum along y-coordinate in pixels. */
    double fwhm_y;
    /** @brief Position angle in degrees. */
    double pa;

    /**
     * @brief Constructor for GaussianParams.
     * @param center_x X-coordinate of the center in pixels
     * @param center_y Y-coordinate of the center in pixels
     * @param amp Amplitude of the component
     * @param fwhm_x Full width at half maximum along x-coordinate in pixels
     * @param fwhm_y Full width at half maximum along y-coordinate in pixels
     * @param pa Position angle in degrees
     */
    GaussianParams(double center_x, double center_y, double amp, double fwhm_x, double fwhm_y, double pa)
        : center_x(center_x), center_y(center_y), amp(amp), fwhm_x(fwhm_x), fwhm_y(fwhm_y), pa(pa) {}

    /**
     * @brief Create a Gaussian component sub-message.
     * @return A Gaussian component sub-message
     */
    CARTA::GaussianComponent GetGaussianComponent() {
        auto center = Message::DoublePoint(center_x, center_y);
        auto fwhm = Message::DoublePoint(fwhm_x, fwhm_y);
        auto component = Message::GaussianComponent(center, amp, fwhm, pa);
        return component;
    };
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_UTIL_H_
