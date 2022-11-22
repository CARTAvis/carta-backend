/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Region.h: class for managing 2D region parameters

#ifndef CARTA_BACKEND_REGION_REGION_H_
#define CARTA_BACKEND_REGION_REGION_H_

#include <atomic>
#include <shared_mutex>
#include <string>
#include <vector>

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/lattices/LRegions/LCRegion.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <casacore/tables/Tables/TableRecord.h>

#include "Util/Image.h"
#include "Util/Message.h"

#define DEFAULT_VERTEX_COUNT 1000

namespace carta {

inline std::string RegionName(CARTA::RegionType type) {
    std::unordered_map<CARTA::RegionType, std::string> region_names = {{CARTA::POINT, "point"}, {CARTA::LINE, "line"},
        {CARTA::POLYLINE, "polyline"}, {CARTA::RECTANGLE, "rectangle"}, {CARTA::ELLIPSE, "ellipse"}, {CARTA::ANNULUS, "annulus"},
        {CARTA::POLYGON, "polygon"}, {CARTA::ANNPOINT, "ann point"}, {CARTA::ANNLINE, "ann line"}, {CARTA::ANNPOLYLINE, "ann polyline"},
        {CARTA::ANNRECTANGLE, "ann rectangle"}, {CARTA::ANNELLIPSE, "ann ellipse"}, {CARTA::ANNPOLYGON, "ann polygon"},
        {CARTA::ANNVECTOR, "vector"}, {CARTA::ANNRULER, "ruler"}, {CARTA::ANNTEXT, "text"}, {CARTA::ANNCOMPASS, "compass"}};
    return region_names[type];
}

struct RegionState {
    // struct used for region parameters
    int reference_file_id;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;

    RegionState() : reference_file_id(-1), type(CARTA::POINT), rotation(0) {}
    RegionState(int ref_file_id_, CARTA::RegionType type_, const std::vector<CARTA::Point>& control_points_, float rotation_)
        : reference_file_id(ref_file_id_), type(type_), control_points(control_points_), rotation(rotation_) {}

    void operator=(const RegionState& other) {
        reference_file_id = other.reference_file_id;
        type = other.type;
        control_points = other.control_points;
        rotation = other.rotation;
    }
    bool operator==(const RegionState& rhs) {
        return (reference_file_id == rhs.reference_file_id) && (type == rhs.type) && !RegionChanged(rhs);
    }
    bool operator!=(const RegionState& rhs) {
        return (reference_file_id != rhs.reference_file_id) || (type != rhs.type) || RegionChanged(rhs);
    }

    bool RegionDefined() {
        return !control_points.empty();
    }

    bool RegionChanged(const RegionState& rhs) {
        // Ignores annotation params (for interrupting region calculations)
        return (rotation != rhs.rotation) || PointsChanged(rhs);
    }
    bool PointsChanged(const RegionState& rhs) {
        // Points must be same size, order, and value to be unchanged
        if (control_points.size() != rhs.control_points.size()) {
            return true;
        }
        for (int i = 0; i < control_points.size(); ++i) {
            float x(control_points[i].x()), y(control_points[i].y());
            float rhs_x(rhs.control_points[i].x()), rhs_y(rhs.control_points[i].y());
            if ((x != rhs_x) || (y != rhs_y)) {
                return true;
            }
        }
        return false;
    }
};

class Region {
public:
    Region(const RegionState& state, std::shared_ptr<casacore::CoordinateSystem> csys);

    inline bool IsValid() { // control points validated
        return _valid;
    };

    // set new region parameters
    bool UpdateRegion(const RegionState& state);

    // state accessors
    inline RegionState GetRegionState() {
        std::lock_guard<std::mutex> guard(_region_state_mutex);
        RegionState region_state = _region_state;
        return region_state;
    }

    inline int GetReferenceFileId() {
        return GetRegionState().reference_file_id;
    }

    inline bool RegionChanged() { // reference image, type, points, or rotation changed
        return _region_changed;
    }

    inline bool IsPoint() {
        return GetRegionState().type == CARTA::POINT;
    }

    inline bool IsLineType() {
        // Not enclosed region defined by 2 or more points
        std::vector<CARTA::RegionType> line_types{
            CARTA::LINE, CARTA::POLYLINE, CARTA::ANNLINE, CARTA::ANNPOLYLINE, CARTA::ANNVECTOR, CARTA::ANNRULER};
        auto type = GetRegionState().type;
        return std::find(line_types.begin(), line_types.end(), type) != line_types.end();
    }

    inline bool IsRotbox() {
        RegionState rs = GetRegionState();
        return ((rs.type == CARTA::RECTANGLE || rs.type == CARTA::ANNRECTANGLE) && (rs.rotation != 0.0));
    }

    inline bool IsAnnotation() {
        return GetRegionState().type > CARTA::POLYGON;
    }

    inline std::shared_ptr<casacore::CoordinateSystem> CoordinateSystem() {
        return _coord_sys;
    }

    // Communication
    bool IsConnected();
    void WaitForTaskCancellation();

    // Converted region as approximate LCPolygon and its mask
    std::shared_ptr<casacore::LCRegion> GetImageRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> image_csys,
        const casacore::IPosition& image_shape, const StokesSource& stokes_source = StokesSource(), bool report_error = true);
    casacore::ArrayLattice<casacore::Bool> GetImageRegionMask(int file_id);

    // Converted region in Record for export
    casacore::TableRecord GetImageRegionRecord(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);

    std::shared_mutex& GetActiveTaskMutex();

private:
    bool SetPoints(const std::vector<CARTA::Point>& points);

    // check points: number required for region type, and values are finite
    bool CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type);
    bool PointsFinite(const std::vector<CARTA::Point>& points);

    // Reset cache when region changes
    void ResetRegionCache();

    // Check if reference region is set successfully
    bool ReferenceRegionValid();

    // Apply region to reference image, set WCRegion and wcs control points.
    void SetReferenceRegion();
    bool RectanglePointsToWorld(std::vector<CARTA::Point>& pixel_points, std::vector<casacore::Quantity>& wcs_points);
    void RectanglePointsToCorners(std::vector<CARTA::Point>& pixel_points, float rotation, casacore::Vector<casacore::Double>& x,
        casacore::Vector<casacore::Double>& y);
    bool EllipsePointsToWorld(std::vector<CARTA::Point>& pixel_points, std::vector<casacore::Quantity>& wcs_points, float& rotation);

    // Reference region as approximate polygon converted to image coordinates; used for data streams
    bool UseApproximatePolygon(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    std::vector<CARTA::Point> GetRectangleMidpoints();
    std::shared_ptr<casacore::LCRegion> GetCachedPolygonRegion(int file_id);
    std::shared_ptr<casacore::LCRegion> GetAppliedPolygonRegion(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    std::vector<std::vector<CARTA::Point>> GetReferencePolygonPoints(int num_vertices);
    std::vector<std::vector<CARTA::Point>> GetApproximatePolygonPoints(int num_vertices);
    std::vector<CARTA::Point> GetApproximateEllipsePoints(int num_vertices);
    double GetTotalSegmentLength(std::vector<CARTA::Point>& points);
    bool ConvertPointsToImagePixels(const std::vector<CARTA::Point>& points, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y);
    void RemoveHorizontalPolygonPoints(casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y);
    bool ValuesNear(float val1, float val2);

    // Region applied to any image; used for export
    std::shared_ptr<casacore::LCRegion> GetCachedLCRegion(int file_id);
    std::shared_ptr<casacore::LCRegion> GetConvertedLCRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        const casacore::IPosition& output_shape, const StokesSource& stokes_source = StokesSource(), bool report_error = true);

    // Control points converted to pixel coords in output image, returned in LCRegion Record format for export
    casacore::TableRecord GetRegionPointsRecord(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    casacore::TableRecord GetControlPointsRecord(const casacore::IPosition& shape);
    void CompleteLCRegionRecord(casacore::TableRecord& record, const casacore::IPosition& shape);
    casacore::TableRecord GetPointRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    casacore::TableRecord GetLineRecord(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> image_csys, const casacore::IPosition& image_shape);
    casacore::TableRecord GetPolygonRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    casacore::TableRecord GetRotboxRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    casacore::TableRecord GetEllipseRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    void CompleteRegionRecord(casacore::TableRecord& record, const casacore::IPosition& image_shape);

    // Utilities to convert control points
    // Input: CARTA::Point. Returns: point (x, y) in reference world coords
    bool ConvertCartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point);
    // Input: point (x,y) in reference world coords. Returns: point (x,y) in output pixel coords
    bool ConvertWorldToPixel(std::vector<casacore::Quantity>& world_point, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        casacore::Vector<casacore::Double>& pixel_point);

    // region parameters struct
    RegionState _region_state;

    // coord sys and shape of reference image
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    // Reference region cache
    std::mutex _region_mutex; // creation of casacore regions is not threadsafe
    std::mutex _region_approx_mutex;
    std::mutex _region_state_mutex;

    // Use a shared lock for long time calculations, use an exclusive lock for the object destruction
    mutable std::shared_mutex _active_task_mutex;

    // Region cached as original type
    std::shared_ptr<casacore::WCRegion> _reference_region; // 2D region applied to reference image
    std::vector<casacore::Quantity> _wcs_control_points;   // for manual region conversion

    // Converted regions
    // Reference region converted to image; key is file_id
    std::unordered_map<int, std::shared_ptr<casacore::LCRegion>> _applied_regions;
    // Polygon approximation region converted to image; key is file_id
    std::unordered_map<int, std::shared_ptr<casacore::LCRegion>> _polygon_regions;

    // region flags
    bool _valid;                // RegionState set properly
    bool _region_changed;       // control points or rotation changed
    bool _reference_region_set; // indicates attempt was made; may be null wcregion outside image

    // Communication
    volatile bool _connected = true;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGION_H_
