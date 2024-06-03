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

namespace carta {

/** @brief A class for calculating initial values used in the image fitting process. */
class InitialValueCalculator {
public:
    /**
     * @brief Constructor for the InitialValueCalculator class.
     * @param image Pointer to the image data
     */
    InitialValueCalculator(float* image);
    /**
     * @brief Calculate initial values from the provided image data.
     * @param initial_values Vector to store the resulting initial values
     * @return Whether the parameters are successfully generated
     */
    bool CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values);

private:
    /** @brief Pointer to the image data. */
    float* _image;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTING_INITIALVALUECALCULATOR_H_
