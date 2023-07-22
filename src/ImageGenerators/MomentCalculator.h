/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
#define CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_

#include <unordered_map>
#include <vector>

namespace carta {

class MomentCalculator {
public:
    MomentCalculator(const std::vector<int>& moment_types);
    ~MomentCalculator() = default;

    void DoCalculation(float* image, size_t length, std::unordered_map<int, float>& results);

private:
    std::vector<int> _moment_types;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
