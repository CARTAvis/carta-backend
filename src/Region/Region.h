/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Region.h: class for managing 2D region parameters in reference image

#ifndef CARTA_SRC_REGION_REGION_H_
#define CARTA_SRC_REGION_REGION_H_

#include <atomic>
#include <shared_mutex>
#include <string>
#include <vector>

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/lattices/LRegions/LCRegion.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <casacore/tables/Tables/TableRecord.h>

#include "RegionConverter.h"
#include "RegionState.h"
#include "Util/Stokes.h"

namespace carta {

class Region {
public:
    Region(const RegionState& state, std::shared_ptr<casacore::CoordinateSystem> csys);

    inline bool IsValid() {
        return _valid;
    };

    inline bool RegionChanged() {
        return _region_changed;
    }

    // Set new region parameters
    bool UpdateRegion(const RegionState& state);

    // RegionState accessors, including region type information
    inline RegionState GetRegionState() {
        std::lock_guard<std::mutex> guard(_region_state_mutex);
        RegionState region_state = _region_state;
        return region_state;
    }

    inline bool IsPoint() {
        return GetRegionState().IsPoint();
    }

    inline bool IsLineType() {
        // Not enclosed region defined by 2 or more points
        return GetRegionState().IsLineType();
    }

    inline bool IsInReferenceImage(int file_id) {
        return file_id == GetRegionState().reference_file_id;
    }

    inline bool IsAnnotation() {
        return GetRegionState().IsAnnotation();
    }

    inline std::shared_ptr<casacore::CoordinateSystem> CoordinateSystem() {
        return _coord_sys;
    }

    // Communication
    bool IsConnected();
    void WaitForTaskCancellation();
    std::shared_mutex& GetActiveTaskMutex();

    // LCRegion and mask for region applied to image.  Must be a closed region (not line) and not annotation.
    std::shared_ptr<casacore::LCRegion> GetImageRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> csys,
        const casacore::IPosition& shape, const StokesSource& stokes_source = StokesSource(), bool report_error = true);
    casacore::ArrayLattice<casacore::Bool> GetImageRegionMask(int file_id);

    // Record for region applied to image, for export.  Not for converting to LCRegion for analytics.
    casacore::TableRecord GetImageRegionRecord(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> csys, const casacore::IPosition& shape);

private:
    // Check points: number required for region type, and values are finite
    bool CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type);
    bool PointsFinite(const std::vector<CARTA::Point>& points);

    // Cached LCRegion
    void ResetRegionCache();
    std::shared_ptr<casacore::LCRegion> GetCachedLCRegion(int file_id, const StokesSource& stokes_source);

    // Record in pixel coordinates from control points, for reference image
    casacore::TableRecord GetControlPointsRecord(const casacore::IPosition& shape);
    casacore::TableRecord GetRotboxRecordForLCRegion(const casacore::IPosition& shape);
    void CompleteRegionRecord(casacore::TableRecord& record, const casacore::IPosition& image_shape);

    // Coordinate system of reference image
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    // Region parameters
    RegionState _region_state;
    std::mutex _region_state_mutex;

    // Region flags
    bool _valid;          // RegionState set properly
    bool _region_changed; // control points or rotation changed

    // Region applied to reference image
    std::shared_ptr<casacore::LCRegion> _lcregion;
    std::mutex _lcregion_mutex;
    bool _lcregion_set; // may be nullptr if outside image

    // Converter to handle region applied to matched image
    std::unique_ptr<RegionConverter> _region_converter;

    // Communication:
    // Use a shared lock for long time calculations, use an exclusive lock for the object destruction
    mutable std::shared_mutex _active_task_mutex;
    volatile bool _connected = true;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGION_H_
