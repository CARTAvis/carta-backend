/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#define DEFAULT_VERTEX_COUNT 1000

namespace carta {

inline std::string RegionName(CARTA::RegionType type) {
    std::unordered_map<CARTA::RegionType, std::string> region_names = {{CARTA::RegionType::POINT, "point"},
        {CARTA::RegionType::LINE, "line"}, {CARTA::RegionType::POLYLINE, "polyline"}, {CARTA::RegionType::RECTANGLE, "rectangle"},
        {CARTA::RegionType::ELLIPSE, "ellipse"}, {CARTA::RegionType::ANNULUS, "annulus"}, {CARTA::RegionType::POLYGON, "polygon"}};
    return region_names[type];
}

struct RegionState {
    // struct used for region parameters
    int reference_file_id;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;

    RegionState() : reference_file_id(-1), type(CARTA::RegionType::POINT), rotation(0) {}
    RegionState(int ref_file_id_, CARTA::RegionType type_, std::vector<CARTA::Point> control_points_, float rotation_)
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
    Region(const RegionState& state, casacore::CoordinateSystem* csys);
    ~Region();

    inline bool IsValid() { // control points validated
        return _valid;
    };

    // set new region parameters
    bool UpdateRegion(const RegionState& state);

    // state accessors
    inline RegionState GetRegionState() {
        return _region_state;
    }

    inline int GetReferenceFileId() {
        return _region_state.reference_file_id;
    }

    inline bool IsRotbox() {
        return ((_region_state.type == CARTA::RegionType::RECTANGLE) && (_region_state.rotation != 0.0));
    }

    inline bool RegionChanged() { // reference image, type, points, or rotation changed
        return _region_changed;
    }

    inline bool IsAnnotation() {
        CARTA::RegionType type = _region_state.type;
        return ((type == CARTA::RegionType::LINE) || (type == CARTA::RegionType::POLYLINE));
    }

    inline casacore::CoordinateSystem* CoordinateSystem() {
        return _coord_sys;
    }

    // Communication
    bool IsConnected();
    void WaitForTaskCancellation();

    // Converted region as approximate LCPolygon and its mask
    casacore::LCRegion* GetImageRegion(
        int file_id, const casacore::CoordinateSystem& image_csys, const casacore::IPosition& image_shape, bool report_error = true);
    casacore::ArrayLattice<casacore::Bool> GetImageRegionMask(int file_id);

    // Converted region in Record for export
    casacore::TableRecord GetImageRegionRecord(
        int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape);

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
    bool UseApproximatePolygon(const casacore::CoordinateSystem& output_csys);
    std::vector<CARTA::Point> GetRectangleMidpoints();
    casacore::LCRegion* GetCachedPolygonRegion(int file_id);
    casacore::LCRegion* GetAppliedPolygonRegion(
        int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape);
    std::vector<CARTA::Point> GetReferencePolygonPoints(int num_vertices);
    std::vector<CARTA::Point> GetApproximatePolygonPoints(int num_vertices);
    std::vector<CARTA::Point> GetApproximateEllipsePoints(int num_vertices);
    double GetTotalSegmentLength(std::vector<CARTA::Point>& points);
    bool ConvertPointsToImagePixels(const std::vector<CARTA::Point>& points, const casacore::CoordinateSystem& output_csys,
        casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y);

    // Region applied to any image; used for export
    casacore::LCRegion* GetCachedLCRegion(int file_id);
    casacore::LCRegion* GetConvertedLCRegion(
        int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape, bool report_error = true);

    // Control points converted to pixel coords in output image, returned in LCRegion Record format for export
    casacore::TableRecord GetRegionPointsRecord(
        int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape);
    casacore::TableRecord GetControlPointsRecord(const casacore::IPosition& shape);
    void CompleteLCRegionRecord(casacore::TableRecord& record, const casacore::IPosition& shape);
    casacore::TableRecord GetPointRecord(const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape);
    casacore::TableRecord GetPolygonRecord(const casacore::CoordinateSystem& output_csys);
    casacore::TableRecord GetRotboxRecord(const casacore::CoordinateSystem& output_csys);
    casacore::TableRecord GetEllipseRecord(const casacore::CoordinateSystem& output_csys);
    casacore::TableRecord GetAnnotationRegionRecord(
        int file_id, const casacore::CoordinateSystem& image_csys, const casacore::IPosition& image_shape);
    void CompleteRegionRecord(casacore::TableRecord& record, const casacore::IPosition& image_shape);

    // Utilities to convert control points
    // Input: CARTA::Point. Returns: point (x, y) in reference world coords
    bool ConvertCartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point);
    // Input: point (x,y) in reference world coords. Returns: point (x,y) in output pixel coords
    bool ConvertWorldToPixel(std::vector<casacore::Quantity>& world_point, const casacore::CoordinateSystem& output_csys,
        casacore::Vector<casacore::Double>& pixel_point);

    // region parameters struct
    RegionState _region_state;

    // coord sys and shape of reference image
    casacore::CoordinateSystem* _coord_sys;

    // Reference region cache
    std::mutex _region_mutex; // creation of casacore regions is not threadsafe
    std::mutex _region_approx_mutex;

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
