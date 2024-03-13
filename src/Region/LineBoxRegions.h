/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// LineBoxRegions.h: class to approximate line with width as series of box regions

#ifndef CARTA_SRC_REGION_LINEBOXREGIONS_H_
#define CARTA_SRC_REGION_LINEBOXREGIONS_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/defs.pb.h>

#include "Region.h"

namespace carta {

class LineBoxRegions {
public:
    LineBoxRegions() = default;
    ~LineBoxRegions() = default;

    bool GetLineBoxRegions(const RegionState& line_region_state, std::shared_ptr<casacore::CoordinateSystem> line_coord_sys, int line_width,
        casacore::Quantity& region_increment, std::vector<RegionState>& region_states, std::string& message);

private:
    // Try setting points on line for regions at fixed-pixel increment
    bool GetFixedPixelRegions(const RegionState& line_region_state, std::shared_ptr<casacore::CoordinateSystem> line_coord_sys,
        int line_width, double& increment, std::vector<RegionState>& region_states);
    float GetLineRotation(const std::vector<double>& line_start, const std::vector<double>& line_end);
    bool CheckLinearOffsets(
        const std::vector<std::vector<double>>& box_centers, std::shared_ptr<casacore::CoordinateSystem> csys, double& increment);
    double GetPointSeparation(
        std::shared_ptr<casacore::CoordinateSystem> coord_sys, const std::vector<double>& point1, const std::vector<double>& point2);
    double GetSeparationTolerance(std::shared_ptr<casacore::CoordinateSystem> csys);

    // Non-linear image: set points on line for polygon (box) regions at fixed-angular increment
    bool GetFixedAngularRegions(const RegionState& line_region_state, std::shared_ptr<casacore::CoordinateSystem> line_coord_sys,
        int line_width, double& increment, std::vector<RegionState>& region_states, std::string& message);
    std::vector<double> FindPointAtTargetSeparation(std::shared_ptr<casacore::CoordinateSystem> coord_sys,
        const std::vector<double>& start_point, const std::vector<double>& end_point, double target_separation, double tolerance);
    RegionState GetPolygonRegionState(std::shared_ptr<casacore::CoordinateSystem> coord_sys, int file_id,
        const std::vector<double>& box_start, const std::vector<double>& box_end, int pixel_width, double angular_width,
        float line_rotation, double tolerance);

    // Convert increment (in arcsec) to different unit depending on scale
    casacore::Quantity AdjustIncrementUnit(double offset_increment, size_t num_offsets);

    // Lock casacore::DirectionCoordinate pixel-MVDirection conversion.
    // Need to lock across instances when calculating PV and line spatial profiles concurrently.
    inline static std::mutex _mvdir_mutex;
};

} // namespace carta

#endif // CARTA_SRC_REGION_LINEBOXREGIONS_H_
