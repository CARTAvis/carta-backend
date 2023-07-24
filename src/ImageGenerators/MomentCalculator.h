/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
#define CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_

#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/ImageInterface.h>

#include <unordered_map>
#include <vector>

namespace carta {

class MomentCalculator {
public:
    MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, const std::vector<int>& moment_types);
    ~MomentCalculator() = default;

    void DoCalculation(float* data, size_t length, std::unordered_map<int, float>& results);

private:
    double GetDeltaVelocity();
    double GetVelocity(double chan);
    double FindMedian(std::vector<float>& array);

    std::shared_ptr<casacore::ImageInterface<float>> _image;
    casacore::CoordinateSystem _coord_sys;
    casacore::SpectralCoordinate _spectral_coord;

    // Moment Types:
    // 0: AVERAGE
    // 1: INTEGRATED
    // 2: WEIGHTED_MEAN_COORDINATE
    // 3: WEIGHTED_DISPERSION_COORDINATE
    // 4: MEDIAN
    // 6: STANDARD_DEVIATION
    // 7: RMS
    // 8: ABS_MEAN_DEVIATION
    // 9: MAXIMUM
    // 10: MAXIMUM_COORDINATE
    // 11: MINIMUM
    // 12: MINIMUM_COORDINATE
    std::vector<int> _moment_types;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
