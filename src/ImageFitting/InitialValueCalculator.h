/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_
#define CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_

#include <random>
#include <vector>

#include <carta-protobuf/fitting_request.pb.h>

#include "ImageFitting/Util.h"
#include "Logger/Logger.h"
#include "Util/Message.h"

namespace carta {

/** @brief A class for calculating initial values used in the image fitting process. */
class InitialValueCalculator {
public:
    /**
     * @brief Constructor for the InitialValueCalculator class.
     * @param fit_data Fitting-related data
     * @param initial_values Vector to store the resulting initial values
     * @param image_std Standard deviation of the image data
     */
    InitialValueCalculator(FitData* fit_data, std::vector<CARTA::GaussianComponent>& initial_values, float image_std);
    /**
     * @brief Calculate initial values from the provided image data.
     * @return Whether the parameters are successfully generated
     */
    bool CalculateInitialValues();

    static std::string GetLog(std::vector<CARTA::GaussianComponent>& initial_values, std::string image_unit);

private:
    /** @brief Pointer to the image data. */
    float* _image;
    /** @brief The width of the image. */
    int _width;
    /** @brief The height of the image. */
    int _height;
    /** @brief X-axis offset from the fitting region to the entire image. */
    size_t _offset_x;
    /** @brief Y-axis offset from the fitting region to the entire image. */
    size_t _offset_y;
    /** @brief Initial values of the fixed and unfixed Gaussian parameters. */
    std::vector<CARTA::GaussianComponent>& _initial_values;
    /** @brief Standard deviation of the image data. */
    double _image_std;

    std::vector<std::tuple<double, double, double, double, double, double>> MethodOfMoments(std::vector<int> centroid_indexes = {0},
        bool apply_filter = false, std::vector<double> center_x = {}, std::vector<double> center_y = {}, std::vector<double> radius = {});
    std::vector<int> KMeansPlusPlus(size_t num_components, float threshold);
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_
