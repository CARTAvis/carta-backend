/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
#define CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_

#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/TempImage.h>

#include <unordered_map>
#include <vector>

namespace carta {

class MomentCalculator {
public:
    MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image);
    ~MomentCalculator() = default;

    void SetInExcludeRange(const std::vector<float>& include_pix, const std::vector<float>& exclude_pix);
    void SetMomentTypes(const std::vector<int>& moment_types);

    std::vector<std::shared_ptr<casacore::ImageInterface<float>>> CreateMoments(float* image_data, int moment_axis);

    void StopCalculation();

private:
    static double FindMedian(std::vector<float>& array, size_t depth);
    bool RequiredMomentType(int type);
    void DoCalculation(float* data, int x, int y, size_t width, size_t height, size_t depth, const casacore::IPosition& start_pos,
        std::unordered_map<int, casacore::Array<float>>& moment_data, std::unordered_map<int, casacore::Array<bool>>& mask_data);

    // Image properties
    std::shared_ptr<casacore::ImageInterface<float>> _image;
    casacore::CoordinateSystem _coord_sys;
    std::vector<double> _velocities;
    double _delta_velocity;

    // Moment requirements

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

    // Pixel range
    std::vector<float> _pixel_range;
    bool _do_include;
    bool _do_exclude;

    // Cancel moments calculation
    volatile bool _cancelled;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATOR_MOMENTCOLLAPSER_H_
