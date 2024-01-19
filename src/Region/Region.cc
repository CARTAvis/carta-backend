/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/RegionType.h>

#include "RegionConverter.h"
// #include "Util/Image.h"

using namespace carta;

Region::Region(const RegionState& state, std::shared_ptr<casacore::CoordinateSystem> csys)
    : _coord_sys(csys), _valid(false), _region_changed(false), _lcregion_set(false), _region_state(state) {
    _valid = CheckPoints(state.control_points, state.type);
}

// *************************************************************************
// Region settings

bool Region::UpdateRegion(const RegionState& new_state) {
    // Update region from region state
    bool valid = CheckPoints(new_state.control_points, new_state.type);

    if (valid) {
        // discern changes
        auto state = GetRegionState();
        _region_changed = (state.RegionChanged(new_state));
        if (_region_changed) {
            ResetRegionCache();
        }

        // set new region state
        std::lock_guard<std::mutex> guard(_region_state_mutex);
        _region_state = new_state;
    } else { // keep existing state
        _region_changed = false;
    }

    return valid;
}

void Region::ResetRegionCache() {
    // Invalid when region changes
    _lcregion_set = false;
    _region_converter.reset();
}

// *************************************************************************
// Parameter checking

bool Region::CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type) {
    // check number of points and that values are finite
    bool points_ok(false);
    size_t npoints(points.size());

    switch (type) {
        case CARTA::POINT:
        case CARTA::ANNPOINT: { // [(x,y)] single point
            points_ok = (npoints == 1) && PointsFinite(points);
            break;
        }
        case CARTA::LINE:
        case CARTA::ANNLINE:
        case CARTA::ANNVECTOR:
        case CARTA::ANNRULER: { // [(x1, y1), (x2, y2)] two points
            points_ok = (npoints == 2) && PointsFinite(points);
            break;
        }
        case CARTA::POLYLINE:
        case CARTA::POLYGON:
        case CARTA::ANNPOLYLINE:
        case CARTA::ANNPOLYGON: { // npoints > 2
            points_ok = (npoints > 2) && PointsFinite(points);
            break;
        }
        case CARTA::RECTANGLE:    // [(cx, cy), (width, height)]
        case CARTA::ANNRECTANGLE: // [(cx, cy), (width, height)]
        case CARTA::ELLIPSE:      // [(cx,cy), (bmaj, bmin)]
        case CARTA::ANNELLIPSE:   // [(cx,cy), (bmaj, bmin)]
        case CARTA::ANNCOMPASS: { // [(cx, cy), (length, length)]
            points_ok = (npoints == 2) && PointsFinite(points) && (points[1].x() > 0) && (points[1].y() > 0);
            break;
        }
        case CARTA::ANNTEXT: { // [(cx, cy), (width, height)]
            // width/height may be 0 on import, frontend will dynamically size textbox
            points_ok = (npoints == 2) && PointsFinite(points);
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

// *************************************************************************
// Region connection state (disconnected when region closed)

bool Region::IsConnected() {
    return _connected;
}

void Region::WaitForTaskCancellation() { // to interrupt the running jobs in the Region
    _connected = false;
    std::unique_lock lock(GetActiveTaskMutex());
}

std::shared_mutex& Region::GetActiveTaskMutex() {
    return _active_task_mutex;
}

// *************************************************************************
// Apply region to image and return LCRegion, mask or Record

std::shared_ptr<casacore::LCRegion> Region::GetImageRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> csys,
    const casacore::IPosition& image_shape, const StokesSource& stokes_source, bool report_error) {
    // Return lattice-coordinate region applied to image and/or computed stokes.
    // Returns nullptr if is annotation, is not a closed region (line/polyline), or outside image.
    std::shared_ptr<casacore::LCRegion> lcregion;

    if (IsAnnotation() || IsLineType()) {
        return lcregion;
    }

    // Check cache
    lcregion = GetCachedLCRegion(file_id, stokes_source);

    if (!lcregion) {
        if (IsInReferenceImage(file_id)) {
            if (!_lcregion_set) {
                // Create LCRegion from TableRecord
                casacore::TableRecord region_record;
                if (GetRegionState().IsRotbox()) {
                    // ControlPointsRecord is unrotated box for export, apply rotation for LCRegion
                    region_record = GetRotboxRecordForLCRegion(image_shape);
                } else {
                    region_record = GetControlPointsRecord(image_shape);
                }

                try {
                    lcregion.reset(casacore::LCRegion::fromRecord(region_record, ""));
                } catch (const casacore::AipsError& err) {
                    // Region is outside image
                }
                _lcregion_set = true;

                // Cache LCRegion
                if (lcregion && stokes_source.IsOriginalImage()) {
                    std::lock_guard<std::mutex> guard(_lcregion_mutex);
                    _lcregion = lcregion;
                }
            }
        } else {
            if (!_region_converter) {
                _region_converter.reset(new RegionConverter(GetRegionState(), _coord_sys));
            }
            return _region_converter->GetImageRegion(file_id, csys, image_shape, stokes_source, report_error);
        }
    }

    return lcregion;
}

std::shared_ptr<casacore::LCRegion> Region::GetCachedLCRegion(int file_id, const StokesSource& stokes_source) {
    // Return cached LCRegion applied to image
    std::shared_ptr<casacore::LCRegion> lcregion;
    if (!stokes_source.IsOriginalImage()) {
        return lcregion;
    }

    if (IsInReferenceImage(file_id)) {
        // Return _lcregion even if unassigned
        std::lock_guard<std::mutex> guard(_lcregion_mutex);
        return _lcregion;
    } else {
        if (!_region_converter) {
            _region_converter.reset(new RegionConverter(GetRegionState(), _coord_sys));
        }
        return _region_converter->GetCachedLCRegion(file_id);
    }

    // Not cached
    return lcregion;
}

casacore::ArrayLattice<casacore::Bool> Region::GetImageRegionMask(int file_id) {
    // Return pixel mask for region applied to image.
    // Requires that LCRegion for this file id has been set and cached (via GetImageRegion),
    // else returns empty mask.
    casacore::ArrayLattice<casacore::Bool> mask;
    auto stokes_source = StokesSource();
    auto lcregion = GetCachedLCRegion(file_id, stokes_source);

    if (lcregion) {
        // LCRegion is an extension region or a fixed region, depending on whether image is reference or matched.
        auto extended_region = dynamic_cast<casacore::LCExtension*>(lcregion.get());
        if (extended_region) {
            auto& fixed_region = static_cast<const casacore::LCRegionFixed&>(extended_region->region());
            mask = fixed_region.getMask();
        } else {
            auto fixed_region = dynamic_cast<casacore::LCRegionFixed*>(lcregion.get());
            if (fixed_region) {
                mask = fixed_region->getMask();
            }
        }
    }

    return mask;
}

casacore::TableRecord Region::GetImageRegionRecord(
    int file_id, std::shared_ptr<casacore::CoordinateSystem> csys, const casacore::IPosition& image_shape) {
    // Return Record describing any region type applied to image, in pixel coordinates.
    // Can be a line or annotation region. Rotated box is a polygon describing corners *without rotation*, for export.
    casacore::TableRecord record;

    if (IsInReferenceImage(file_id)) {
        record = GetControlPointsRecord(image_shape);
    } else {
        if (!_region_converter) {
            _region_converter.reset(new RegionConverter(GetRegionState(), _coord_sys));
        }
        record = _region_converter->GetImageRegionRecord(file_id, csys, image_shape);
    }

    if (!record.isDefined("isRegion")) {
        // Record created from control points instead of LCRegion::toRecord
        CompleteRegionRecord(record, image_shape);
    }
    return record;
}

// ***************************************************************

casacore::TableRecord Region::GetControlPointsRecord(const casacore::IPosition& image_shape) {
    // Return region Record in pixel coords in format of LCRegion::toRecord() from control points.
    // Rotated box is returned as unrotated LCBox, rotation retrieved from RegionState.
    // For rotated box for analytics, use GetRotboxRecordForLCRegion() as polygon.
    casacore::TableRecord record;

    auto region_state = GetRegionState();
    auto region_type = region_state.type;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // Box with blc=trc, same ndim as image (chan/stokes range not used so just 0)
            auto ndim = image_shape.size();
            casacore::Vector<casacore::Float> blc(ndim, 0.0), trc(ndim, 0.0);
            blc(0) = region_state.control_points[0].x();
            blc(1) = region_state.control_points[0].y();
            trc(0) = region_state.control_points[0].x();
            trc(1) = region_state.control_points[0].y();

            record.define("name", "LCBox");
            record.define("blc", blc);
            record.define("trc", trc);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNTEXT: {
            // Rectangle is LCPolygon with 4 corners; calculate from center and width/height
            casacore::Vector<casacore::Double> x, y;
            bool apply_rotation(false);
            if (GetRegionState().GetRectangleCorners(x, y, apply_rotation)) {
                // LCPolygon::toRecord includes last point as first point to close region
                x.resize(5, true);
                y.resize(5, true);
                x(4) = x(0);
                y(4) = y(0);

                record.define("name", "LCPolygon");
                record.define("x", x);
                record.define("y", y);
            }
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNVECTOR:
        case CARTA::RegionType::ANNRULER: {
            // Define "x" and "y" pixel values
            size_t npoints(region_state.control_points.size());
            casacore::Vector<casacore::Float> x(npoints), y(npoints);
            for (size_t i = 0; i < npoints; ++i) {
                x(i) = region_state.control_points[i].x();
                y(i) = region_state.control_points[i].y();
            }

            if (region_type == CARTA::RegionType::POLYGON || region_type == CARTA::RegionType::ANNPOLYGON) {
                // LCPolygon::toRecord includes first point as last point to close region
                x.resize(npoints + 1, true);
                x(npoints) = region_state.control_points[0].x();
                y.resize(npoints + 1, true);
                y(npoints) = region_state.control_points[0].y();

                record.define("name", "LCPolygon");
            } else {
                // CARTA USE ONLY, line region names not in casacore because not an LCRegion
                record.define("name", _region_state.GetLineRegionName());
            }
            record.define("x", x);
            record.define("y", y);
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS: {
            casacore::Vector<casacore::Float> center(2), radii(2);
            center(0) = region_state.control_points[0].x();
            center(1) = region_state.control_points[0].y();
            radii(0) = region_state.control_points[1].x();
            radii(1) = region_state.control_points[1].y();

            if (region_type == CARTA::RegionType::ANNCOMPASS) {
                record.define("name", "compass");
            } else {
                record.define("name", "LCEllipsoid");
            }
            record.define("center", center);
            record.define("radii", radii);

            // LCEllipsoid measured from major (x) axis
            casacore::Quantity theta = casacore::Quantity(region_state.rotation + 90.0, "deg");
            theta.convert("rad");
            record.define("theta", theta.getValue());
            break;
        }
        default: // Annulus not implemented
            break;
    }
    CompleteRegionRecord(record, image_shape);
    return record;
}

casacore::TableRecord Region::GetRotboxRecordForLCRegion(const casacore::IPosition& image_shape) {
    // Convert rotated box corners to polygon and create LCPolygon-type record
    casacore::TableRecord record;
    casacore::Vector<casacore::Double> x, y;
    if (GetRegionState().GetRectangleCorners(x, y)) {
        // LCPolygon::toRecord includes last point as first point to close region
        x.resize(5, true);
        y.resize(5, true);
        x(4) = x(0);
        y(4) = y(0);

        record.define("name", "LCPolygon");
        record.define("x", x);
        record.define("y", y);
    }
    CompleteRegionRecord(record, image_shape);
    return record;
}

void Region::CompleteRegionRecord(casacore::TableRecord& record, const casacore::IPosition& image_shape) {
    // Add common Record fields for record defining region
    if (!record.empty()) {
        record.define("isRegion", casacore::RegionType::LC);
        record.define("comment", "");
        record.define("oneRel", false); // control points are 0-based

        casacore::Vector<int> record_shape;
        if (_region_state.IsPoint()) {
            // LCBox uses entire image shape
            record_shape = image_shape.asVector();
        } else {
            // Other regions use 2D shape
            record_shape.resize(2);
            record_shape(0) = image_shape(0);
            record_shape(1) = image_shape(1);
        }
        record.define("shape", record_shape);
    }
}
