//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, const float rotation,
    casacore::CoordinateSystem& csys)
    : _valid(false), _region_state_changed(false), _region_changed(false) {
    // validate and set region parameters
    _valid = UpdateState(name, type, points, rotation, csys);
}

// *************************************************************************
// Region settings

bool Region::UpdateState(const std::string name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
    casacore::CoordinateSystem& csys) {
    // Set region parameters and flags for state change
    bool valid_points = CheckPoints(points, type);
    if (valid_points) {
        RegionState new_state;
        new_state.name = name;
        new_state.type = type;
        new_state.control_points = points;
        new_state.rotation = rotation;

        // discern changes
        _region_state_changed = (_region_state != new_state);
        _region_changed = (_region_state.RegionChanged(new_state));

        // set new region state and coord sys
        _region_state = new_state;
        _coord_sys = csys;
    } else { // keep existing state
        _region_state_changed = false;
        _region_changed = false;
    }
    return valid_points;
}

double Region::AngleToLength(casacore::Quantity angle, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse radius.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.
    int coord, coord_axis;
    _coord_sys.findWorldAxis(coord, coord_axis, pixel_axis);
    casacore::Vector<casacore::String> units = _coord_sys.directionCoordinate().worldAxisUnits();
    angle.convert(units[coord_axis]);
    casacore::Vector<casacore::Double> increments(_coord_sys.directionCoordinate().increment());
    return fabs(angle.getValue() / increments[coord_axis]);
}

bool Region::CartaPointToWorld(const CARTA::Point& point, casacore::Vector<casacore::Quantity>& world_point) {
    // Convert CARTA point (in pixel coordinates) to world coordinates.
    // Returns world_point vector and boolean flag to indicate a successful conversion.
    bool converted(false);

    // Vectors must be same number of axes as in coord system for conversion, even though we only need xy axes
    int naxes(_coord_sys.nPixelAxes());
    casacore::Vector<casacore::Double> pixel_coords(naxes), world_coords(naxes);
    pixel_coords = 0.0;
    pixel_coords(0) = point.x();
    pixel_coords(1) = point.y();

    // convert pixel vector to world vector
    if (_coord_sys.toWorld(world_coords, pixel_coords)) {
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        world_point.resize(2);
        world_point(0) = casacore::Quantity(world_coords(0), world_units(0));
        world_point(1) = casacore::Quantity(world_coords(1), world_units(1));
        converted = true;
    }
    return converted;
}

// *************************************************************************
// Parameter checking

bool Region::CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type) {
    // check number of points and that values are finite
    bool points_ok(false);
    size_t npoints(points.size());
    switch (type) {
        case CARTA::POINT: { // [(x, y)]
            points_ok = (npoints == 1) && PointsFinite(points);
            break;
        }
        case CARTA::RECTANGLE: { // [(cx,cy), (width,height)], width/height > 0
            points_ok = (npoints == 2) && PointsFinite(points) && (points[1].x() > 0) && (points[1].y() > 0);
            break;
        }
        case CARTA::ELLIPSE: { // [(cx,cy), (bmaj, bmin)]
            points_ok = (npoints == 2) && PointsFinite(points);
            break;
        }
        case CARTA::POLYGON: { // any number of (x, y) greater than 2, which is a line
            points_ok = (npoints > 2) && PointsFinite(points);
            break;
        }
        default:
            break;
    }

    return points_ok;
}

bool Region::PointsFinite(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_finite(true);
    for (auto& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            points_finite = false;
            break;
        }
    }
    return points_finite;
}

// Region connection state (disconnected when region closed)

bool Region::IsConnected() {
    return _connected;
}

void Region::DisconnectCalled() { // to interrupt the running jobs in the Region
    _connected = false;
}
