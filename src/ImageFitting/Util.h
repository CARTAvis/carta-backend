/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTING_UTIL_H_
#define CARTA_SRC_IMAGEFITTING_UTIL_H_

#include <vector>

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

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_UTIL_H_
