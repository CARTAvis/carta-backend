/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionConverter.h: class for managing region conversion to matched image

#ifndef CARTA_SRC_REGION_REGIONCONVERTER_H_
#define CARTA_SRC_REGION_REGIONCONVERTER_H_

#include <atomic>
#include <string>
#include <vector>

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/lattices/LRegions/LCRegion.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <casacore/tables/Tables/TableRecord.h>

#include "RegionState.h"
#include "Util/Stokes.h"

#define DEFAULT_VERTEX_COUNT 1000

namespace carta {

class RegionConverter {
public:
    RegionConverter(const RegionState& state, std::shared_ptr<casacore::CoordinateSystem> csys);

    // LCRegion and mask for region applied to image.  Must be a closed region (not line) and not annotation.
    std::shared_ptr<casacore::LCRegion> GetCachedLCRegion(int file_id, bool use_approx_polygon = true);
    std::shared_ptr<casacore::LCRegion> GetImageRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> csys,
        const casacore::IPosition& shape, const StokesSource& stokes_source = StokesSource(), bool report_error = true);
    casacore::TableRecord GetImageRegionRecord(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> csys, const casacore::IPosition& shape);

private:
    // Reference region in world coordinates (reference coordinate system)
    void SetReferenceWCRegion();
    bool ReferenceRegionValid();
    bool RectangleControlPointsToWorld(std::vector<casacore::Quantity>& wcs_points);
    bool EllipseControlPointsToWorld(std::vector<casacore::Quantity>& wcs_points, float& ellipse_rotation);
    bool CartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point);

    // Region converted directly to image (not polygon approximation)
    std::shared_ptr<casacore::LCRegion> GetConvertedLCRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        const casacore::IPosition& output_shape, const StokesSource& stokes_source = StokesSource(), bool report_error = true);

    // Reference region converted to image as approximate polygon, cached with file_id
    bool UseApproximatePolygon(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    std::vector<CARTA::Point> GetRectangleMidpoints();
    std::shared_ptr<casacore::LCRegion> GetAppliedPolygonRegion(
        int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    std::vector<std::vector<CARTA::Point>> GetReferencePolygonPoints(int num_vertices);
    std::vector<std::vector<CARTA::Point>> GetApproximatePolygonPoints(int num_vertices);
    std::vector<CARTA::Point> GetApproximateEllipsePoints(int num_vertices);
    double GetTotalSegmentLength(std::vector<CARTA::Point>& points);
    void RemoveHorizontalPolygonPoints(casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y);
    bool ValuesNear(float val1, float val2);

    // Record for region converted to pixel coords in output image, returned in LCRegion Record format
    casacore::TableRecord GetRegionPointsRecord(
        std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    void CompleteLCRegionRecord(casacore::TableRecord& record, const casacore::IPosition& shape);
    casacore::TableRecord GetPointRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape);
    casacore::TableRecord GetLineRecord(std::shared_ptr<casacore::CoordinateSystem> image_csys);
    casacore::TableRecord GetPolygonRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    casacore::TableRecord GetRotboxRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);
    casacore::TableRecord GetEllipseRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys);

    // Utilities for pixel/world conversion
    bool PointsToImagePixels(const std::vector<CARTA::Point>& points, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y);
    // World point as (x,y) quantities to output pixel point as (x,y) vector
    bool WorldPointToImagePixels(std::vector<casacore::Quantity>& world_point, std::shared_ptr<casacore::CoordinateSystem> output_csys,
        casacore::Vector<casacore::Double>& pixel_point);

    // Reference image region parameters
    RegionState _region_state;
    std::shared_ptr<casacore::CoordinateSystem> _reference_coord_sys;

    // Reference region: control points converted to world coords,
    // cached as WCRegion, and flag that WCRegion was attempted
    // (may be null if outside coordinate system)
    std::vector<casacore::Quantity> _wcs_control_points;
    std::shared_ptr<casacore::WCRegion> _reference_region;
    bool _reference_region_set;

    // Converted regions: reference region applied to image,
    // or as polygon approximation applied to image.
    // Map key is file_id
    std::mutex _region_mutex;
    std::unordered_map<int, std::shared_ptr<casacore::LCRegion>> _converted_regions;
    std::unordered_map<int, std::shared_ptr<casacore::LCRegion>> _polygon_regions;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONCONVERTER_H_
