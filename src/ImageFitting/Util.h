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

struct GaussianParams {
    double center_x;
    double center_y;
    double amp;
    double fwhm_x;
    double fwhm_y;
    double pa;

    GaussianParams(double center_x, double center_y, double amp, double fwhm_x, double fwhm_y, double pa)
        : center_x(center_x), center_y(center_y), amp(amp), fwhm_x(fwhm_x), fwhm_y(fwhm_y), pa(pa) {}
};

/**
 * @brief Create a Gaussian component sub-message from Gaussian parameters.
 * @param params A tuple of Gaussian parameters: center x, center y, amplitude, FWHM x, FWHM y, and position angle
 * @return A Gaussian component sub-message
 */
inline CARTA::GaussianComponent GetGaussianComponent(GaussianParams params) {
    auto center = Message::DoublePoint(params.center_x, params.center_y);
    auto fwhm = Message::DoublePoint(params.fwhm_x, params.fwhm_y);
    auto component = Message::GaussianComponent(center, params.amp, fwhm, params.pa);
    return component;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_UTIL_H_
