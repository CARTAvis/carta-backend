/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_
#define CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_

#include <vector>

#include <carta-protobuf/fitting_request.pb.h>

#include "Logger/Logger.h"
#include "Util/Message.h"

namespace carta {

/** @brief A class for calculating initial values used in the image fitting process. */
class InitialValueCalculator {
public:
    /**
     * @brief Constructor for the InitialValueCalculator class.
     * @param image Pointer to the image data
     */
    InitialValueCalculator(float* image, size_t width, size_t height);
    /**
     * @brief Calculate initial values from the provided image data.
     * @param initial_values Vector to store the resulting initial values
     * @return Whether the parameters are successfully generated
     */
    bool CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values);

    static std::string GetLog(std::vector<CARTA::GaussianComponent>& initial_values, std::string image_unit);

private:
    /** @brief Pointer to the image data. */
    float* _image;

    int _width;
    int _height;

    std::tuple<double, double, double, double, double, double> MethodOfMoments(
        bool apply_filter = false, double center_x = 0, double center_y = 0, double radius = 0);
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_
