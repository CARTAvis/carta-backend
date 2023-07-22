/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentCalculator.h"

#include "ImageGenerator.h"
#include "Logger/Logger.h"

using namespace carta;

MomentCalculator::MomentCalculator(const std::vector<int>& moment_types) : _moment_types(moment_types) {}

void MomentCalculator::DoCalculation(float* image, size_t length, std::unordered_map<int, float>& results) {
    double sum(0);
    size_t counts(0);
    for (size_t i = 0; i < length; ++i) {
        if (!std::isnan(image[i])) {
            sum += image[i];
            counts++;
        }
    }

    results[0] = counts == 0 ? std::numeric_limits<float>::quiet_NaN() : sum / (double)counts;
}