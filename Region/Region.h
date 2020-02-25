//# Region.h: class for managing 2D region parameters

#ifndef CARTA_BACKEND_REGION_REGION_H_
#define CARTA_BACKEND_REGION_REGION_H_

#include <string>
#include <vector>

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#include "../Frame.h"

struct RegionState {
    // struct used to determine whether region changed
    int ref_file_id;
    std::string name;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;

    RegionState() {}
    RegionState(int ref_file_id_, std::string name_, CARTA::RegionType type_, std::vector<CARTA::Point> control_points_, float rotation_) {
        ref_file_id = ref_file_id_;
        name = name_;
        type = type_;
        control_points = control_points_;
        rotation = rotation_;
    }
    void UpdateState(
        int ref_file_id_, std::string name_, CARTA::RegionType type_, std::vector<CARTA::Point> control_points_, float rotation_) {
        ref_file_id = ref_file_id_;
        name = name_;
        type = type_;
        control_points = control_points_;
        rotation = rotation_;
    }

    void operator=(const RegionState& other) {
        ref_file_id = other.ref_file_id;
        name = other.name;
        type = other.type;
        control_points = other.control_points;
        rotation = other.rotation;
    }
    bool operator==(const RegionState& rhs) {
        if ((name != rhs.name) || RegionChanged(rhs)) {
            return false;
        }
        return true;
    }
    bool operator!=(const RegionState& rhs) {
        if ((name != rhs.name) || RegionChanged(rhs)) {
            return true;
        }
        return false;
    }

    bool RegionChanged(const RegionState& rhs) { // ignores name change (does not interrupt region calculations)
        return (ref_file_id != rhs.ref_file_id) || (type != rhs.type) || (rotation != rhs.rotation) || PointsChanged(rhs);
    }
    bool PointsChanged(const RegionState& rhs) {
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

namespace carta {

class Region {
public:
    Region(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
        const casacore::CoordinateSystem& csys);

    inline bool IsValid() { // control points validated
        return _valid;
    };

    inline bool IsPointRegion() {
        return (_region_state.type == CARTA::POINT);
    };

    // set new region state and coord sys
    bool UpdateState(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
        const casacore::CoordinateSystem& csys);

    // state accessors
    inline RegionState GetRegionState() {
        return _region_state;
    }
    inline bool RegionStateChanged() { // any params changed
        return _region_state_changed;
    };
    inline bool RegionChanged() { // reference image, type, points, or rotation changed
        return _region_changed;
    }

    // Communication
    bool IsConnected();
    void DisconnectCalled();

private:
    bool SetPoints(const std::vector<CARTA::Point>& points);

    // check points: number required for region type, and values are finite
    bool CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type);
    bool PointsFinite(const std::vector<CARTA::Point>& points);

    // region definition (name, type, control points in pixel coordinates, rotation)
    RegionState _region_state;

    casacore::CoordinateSystem _coord_sys; // coord sys of reference image

    // region flags
    bool _valid;
    bool _region_state_changed; // any parameters changed
    bool _region_changed;       // type, control points, or rotation changed

    // Communication
    volatile bool _connected = true;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGION_H_
