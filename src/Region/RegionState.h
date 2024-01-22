/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionState.h: struct for holding region parameters

#ifndef CARTA_SRC_REGION_REGIONSTATE_H_
#define CARTA_SRC_REGION_REGIONSTATE_H_

#include <carta-protobuf/defs.pb.h>

namespace carta {

struct RegionState {
    // struct used for region parameters
    int reference_file_id;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;

    RegionState() : reference_file_id(-1), type(CARTA::POINT), rotation(0) {}
    RegionState(int ref_file_id_, CARTA::RegionType type_, const std::vector<CARTA::Point>& control_points_, float rotation_)
        : reference_file_id(ref_file_id_), type(type_), control_points(control_points_), rotation(rotation_) {}

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

    bool IsPoint() {
        // Includes annotation types.
        return type == CARTA::POINT || type == CARTA::ANNPOINT;
    }

    bool IsLineType() {
        // Not enclosed region, defined by points. Includes annotation types.
        std::vector<CARTA::RegionType> line_types{
            CARTA::LINE, CARTA::POLYLINE, CARTA::ANNLINE, CARTA::ANNPOLYLINE, CARTA::ANNVECTOR, CARTA::ANNRULER};
        return std::find(line_types.begin(), line_types.end(), type) != line_types.end();
    }

    bool IsBox() {
        // Rectangle-type regions. Includes annotation types.
        std::vector<CARTA::RegionType> rectangle_types{CARTA::RECTANGLE, CARTA::ANNRECTANGLE, CARTA::ANNTEXT};
        return std::find(rectangle_types.begin(), rectangle_types.end(), type) != rectangle_types.end();
    }

    bool IsRotbox() {
        // Rectangle-type regions with rotation. Includes annotation types.
        return IsBox() && (rotation != 0.0);
    }

    bool IsAnnotation() {
        return type > CARTA::POLYGON;
    }

    bool GetRectangleCorners(casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y, bool apply_rotation = true) {
        // Convert rectangle points [[cx, cy], [width, height]] to corner points. Optionally apply rotation.
        if (type != CARTA::RECTANGLE && type != CARTA::ANNRECTANGLE && type != CARTA::ANNTEXT) {
            return false;
        }
        float center_x(control_points[0].x()), center_y(control_points[0].y());
        float width(control_points[1].x()), height(control_points[1].y());
        x.resize(4);
        y.resize(4);

        if (rotation == 0.0 || !apply_rotation) {
            float x_min(center_x - width / 2.0f), x_max(center_x + width / 2.0f);
            float y_min(center_y - height / 2.0f), y_max(center_y + height / 2.0f);
            // Bottom left
            x(0) = x_min;
            y(0) = y_min;
            // Bottom right
            x(1) = x_max;
            y(1) = y_min;
            // Top right
            x(2) = x_max;
            y(2) = y_max;
            // Top left
            x(3) = x_min;
            y(3) = y_max;
        } else {
            // Apply rotation matrix to get width and height vectors in rotated basis
            float cos_x = cos(rotation * M_PI / 180.0f);
            float sin_x = sin(rotation * M_PI / 180.0f);
            float width_vector_x = cos_x * width;
            float width_vector_y = sin_x * width;
            float height_vector_x = -sin_x * height;
            float height_vector_y = cos_x * height;

            // Bottom left
            x(0) = center_x + (-width_vector_x - height_vector_x) / 2.0f;
            y(0) = center_y + (-width_vector_y - height_vector_y) / 2.0f;
            // Bottom right
            x(1) = center_x + (width_vector_x - height_vector_x) / 2.0f;
            y(1) = center_y + (width_vector_y - height_vector_y) / 2.0f;
            // Top right
            x(2) = center_x + (width_vector_x + height_vector_x) / 2.0f;
            y(2) = center_y + (width_vector_y + height_vector_y) / 2.0f;
            // Top left
            x(3) = center_x + (-width_vector_x + height_vector_x) / 2.0f;
            y(3) = center_y + (-width_vector_y + height_vector_y) / 2.0f;
        }
        return true;
    }

    std::string GetLineRegionName() {
        // Names not defined in casacore, for region record
        std::string name;
        if (IsLineType()) {
            std::unordered_map<CARTA::RegionType, std::string> line_region_names = {{CARTA::LINE, "line"}, {CARTA::POLYLINE, "polyline"},
                {CARTA::ANNLINE, "line"}, {CARTA::ANNPOLYLINE, "polyline"}, {CARTA::ANNVECTOR, "vector"}, {CARTA::ANNRULER, "ruler"}};
            name = line_region_names.at(type);
        }
        return name;
    }
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONSTATE_H_
