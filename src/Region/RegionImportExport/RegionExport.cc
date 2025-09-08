/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RegionExport.h"

#include <casacore/casa/OS/File.h>

#include "Logger/Logger.h"

using namespace carta;

RegionExport::RegionExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys), _image_shape(image_shape) {}

bool RegionExport::CanExportToFile(const std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack) {
    // Check ability to create export file if filename given.
    std::string error;
    bool need_overwrite_confirmation(false);

    if (!filename.empty()) {
        casacore::File export_file(filename);
        if (export_file.exists()) {
            if (export_file.isDirectory()) {
                error = "Export region failed: cannot overwrite existing directory.";
            } else if (!export_file.isRegular()) {
                error = "Export region failed: existing path is not a file.";
            } else if (!overwrite) {
                error = "Export region failed: cannot overwrite existing file.";
                need_overwrite_confirmation = true;
            } else if (!export_file.isWritable()) {
                error = "Export region failed: cannot overwrite read-only file.";
            }
        } else if (!export_file.canCreate()) {
            error = "Export region failed: cannot create file.";
        }
    }
    if (!error.empty()) {
        export_ack.set_success(false);
        export_ack.set_message(error);
        export_ack.set_overwrite_confirmation_required(need_overwrite_confirmation);
        return false;
    }
    return true;
}

bool RegionExport::AddExportRegion(
    int file_id, std::shared_ptr<Region> region, const CARTA::RegionStyle& region_style, bool export_pixel_coords) {
    // Export region using RegionState for pixel coords in reference image else from region Record
    auto region_state = region->GetRegionState();
    bool region_exported(false);

    if ((region_state.reference_file_id == file_id) && export_pixel_coords) {
        // Use RegionState control points with reference file id for pixel export
        region_exported = AddExportRegion(region_state, region_style); // CRTF or DS9 export
    } else {
        // Convert region to another image, to world coordinates, or both
        try {
            // Use Record containing pixel coords of region converted to output image
            casacore::TableRecord region_record = region->GetImageRegionRecord(file_id, _coord_sys, _image_shape);
            region_exported = AddExportRegion(region_state, region_style, region_record, export_pixel_coords);
        } catch (const casacore::AipsError& err) {
            spdlog::error("Export region failed: {}", err.getMesg());
        }
    }
    return region_exported;
}

void RegionExport::ExportRegions(const std::string& filename, std::string& message, CARTA::ExportRegionAck& export_ack) {
    // Export regions to file or to contents.  Complete ack message.
    bool success(false);
    if (filename.empty()) {
        // Add contents to ack
        std::vector<std::string> line_contents;
        success = ExportRegions(line_contents, message);
        if (success) {
            *export_ack.mutable_contents() = {line_contents.begin(), line_contents.end()};
        }
    } else {
        // Write to file
        success = ExportRegions(filename, message);
    }
    export_ack.set_success(success);
    export_ack.set_message(message);
}

bool RegionExport::AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style,
    const casacore::RecordInterface& region_record, bool pixel_coord) {
    // Convert Record to Quantities for region type then set region
    // Record is in pixel coords; convert to world coords if needed
    if (pixel_coord) {
        casa::AnnotationBase::unitInit(); // enable "pix" unit
    }

    std::vector<casacore::Quantity> control_points;
    casacore::Quantity rotation(region_state.rotation, "deg");

    // Convert control points and rotation to Quantity; rotation updated for ellipse only
    bool converted(false);
    switch (region_state.type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT:
            converted = ConvertRecordToPoint(region_record, pixel_coord, control_points);
            break;
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNTEXT:
            converted = ConvertRecordToRectangle(region_record, pixel_coord, control_points);
            break;
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS:
            converted = ConvertRecordToEllipse(region_state, region_record, pixel_coord, control_points, rotation);
            break;
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNVECTOR:
        case CARTA::RegionType::ANNRULER:
            converted = ConvertRecordToPolygonLine(region_record, pixel_coord, control_points);
            break;
        default:
            break;
    }

    if (converted) {
        return AddExportRegion(region_state.type, control_points, rotation, region_style); // CRTF or DS9 export
    }
    return converted;
}

bool RegionExport::ConvertRecordToPoint(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to point Quantity control points
    // Point is an LCBox with blc, trc arrays in pixel coordinates (blc = trc)
    casacore::Vector<casacore::Float> blc = region_record.asArrayFloat("blc");

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        blc -= (float)1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(blc(0), "pix"));
        control_points.push_back(casacore::Quantity(blc(1), "pix"));
        return true;
    }

    // For world coords, convert to Double
    casacore::Vector<casacore::Double> pixel_coords(blc.size());
    for (auto i = 0; i < blc.size(); ++i) {
        pixel_coords(i) = blc(i);
    }

    try {
        // Convert pixel to world
        casacore::Vector<casacore::Double> world_coords = _coord_sys->toWorld(pixel_coords);

        // Add Quantities to control_points
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));
        return true;
    } catch (const casacore::AipsError& err) {
        spdlog::error("Export error: point Record conversion failed: {}", err.getMesg());
        return false;
    }
}

bool RegionExport::ConvertRecordToRectangle(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to box Quantity control points.
    // Rectangles are exported to Record as LCPolygon with 4 points: blc, brc, trc, tlc.
    // The input Record for a rotbox must be the corners of an unrotated box (rotation in the region state)
    casacore::Vector<casacore::Double> x, y;

    if (region_record.dataType("x") == casacore::TpArrayFloat) {
        casacore::Vector<casacore::Float> xf, yf;
        xf = region_record.asArrayFloat("x");
        yf = region_record.asArrayFloat("y");

        // Convert to Double
        auto xf_size(xf.size());
        x.resize(xf_size);
        y.resize(xf_size);
        for (auto i = 0; i < xf_size; ++i) {
            x(i) = xf(i);
            y(i) = yf(i);
        }
    } else {
        x = region_record.asArrayDouble("x");
        y = region_record.asArrayDouble("y");
    }

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        x -= 1.0;
        y -= 1.0;
    }

    double cx, cy, width, height;
    casacore::Double blc_x = x[0];
    casacore::Double brc_x = x[1];
    casacore::Double trc_x = x[2];
    casacore::Double tlc_x = x[3];
    casacore::Double blc_y = y[0];
    casacore::Double brc_y = y[1];
    casacore::Double trc_y = y[2];
    casacore::Double tlc_y = y[3];

    // Control points: center point, width/height
    cx = (blc_x + trc_x) / 2.0;
    cy = (blc_y + trc_y) / 2.0;
    width = sqrt(pow((brc_x - blc_x), 2) + pow((brc_y - blc_y), 2));
    height = sqrt(pow((tlc_x - blc_x), 2) + pow((tlc_y - blc_y), 2));

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(cx, "pix"));
        control_points.push_back(casacore::Quantity(cy, "pix"));
        control_points.push_back(casacore::Quantity(width, "pix"));
        control_points.push_back(casacore::Quantity(height, "pix"));
        return true;
    }

    try {
        // Convert center position to world coords
        casacore::Vector<casacore::Double> world_center;
        casacore::Vector<casacore::Double> pixel_center(_coord_sys->nPixelAxes(), 0.0);
        pixel_center(0) = cx;
        pixel_center(1) = cy;
        _coord_sys->toWorld(world_center, pixel_center);

        // Convert width/height to world coords
        casacore::Quantity world_width = _coord_sys->toWorldLength(width, 0);
        casacore::Quantity world_height = _coord_sys->toWorldLength(height, 1);

        // Convert to Quantities and add to control_points
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_center(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_center(1), world_units(1)));
        control_points.push_back(world_width);
        control_points.push_back(world_height);
        return true;
    } catch (const casacore::AipsError& err) {
        spdlog::error("Export error: rectangle Record conversion failed: {}", err.getMesg());
        return false;
    }
}

bool RegionExport::ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
    std::vector<casacore::Quantity>& control_points, casacore::Quantity& rotation) {
    // Convert casacore Record to ellipse Quantity control points
    // RegionState needed to check if bmaj/bmin swapped for LCEllipsoid
    casacore::Vector<casacore::Float> center = region_record.asArrayFloat("center");
    casacore::Vector<casacore::Float> radii = region_record.asArrayFloat("radii");
    casacore::Double theta = region_record.asDouble("theta"); // radians
    rotation = casacore::Quantity(theta, "rad");
    rotation.convert("deg"); // CASA rotang, from x-axis

    CARTA::Point ellipse_axes = region_state.control_points[1];
    bool reversed = (region_state.type == CARTA::RegionType::ELLIPSE || region_state.type == CARTA::RegionType::ANNELLIPSE) &&
                    ((ellipse_axes.x() < ellipse_axes.y()) == (radii(0) > radii(1)));

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        center -= (float)1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(center(0), "pix"));
        control_points.push_back(casacore::Quantity(center(1), "pix"));
        // Restore original axes order; oddly, rotation angle was not changed
        if (reversed) {
            control_points.push_back(casacore::Quantity(radii(1), "pix"));
            control_points.push_back(casacore::Quantity(radii(0), "pix"));
            rotation += 90.0;
            if (rotation.getValue() > 360.0) {
                rotation -= 360.0;
            }
        } else {
            control_points.push_back(casacore::Quantity(radii(0), "pix"));
            control_points.push_back(casacore::Quantity(radii(1), "pix"));
        }
        return true;
    }

    casacore::Vector<casacore::Double> pixel_coords(_image_shape.size());
    pixel_coords = 0.0;
    pixel_coords(0) = center(0);
    pixel_coords(1) = center(1);

    try {
        // Convert center pixel to world and add to control points
        casacore::Vector<casacore::Double> world_coords = _coord_sys->toWorld(pixel_coords);
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));

        // Convert (lattice region) axes pixel to world and add to control points
        casacore::Quantity bmaj = _coord_sys->toWorldLength(radii(0), 0);
        casacore::Quantity bmin = _coord_sys->toWorldLength(radii(1), 1);
        // Restore original axes order; oddly, rotation angle was not changed
        if (reversed) {
            control_points.push_back(bmin);
            control_points.push_back(bmaj);
            rotation += 90.0;
            if (rotation.getValue() > 360.0) {
                rotation -= 360.0;
            }
        } else {
            control_points.push_back(bmaj);
            control_points.push_back(bmin);
        }
        return true;
    } catch (const casacore::AipsError& err) {
        spdlog::error("Export error: ellipse Record conversion failed: {}", err.getMesg());
        return false;
    }
    return false;
}

bool RegionExport::ConvertRecordToPolygonLine(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to polygon Quantity control points
    // Polygon is an LCPolygon with x, y arrays in pixel coordinates
    casacore::String region_name = region_record.asString("name");
    casacore::Vector<casacore::Double> x, y;

    if (region_record.dataType("x") == casacore::TpArrayFloat) {
        casacore::Vector<casacore::Float> xf, yf;
        xf = region_record.asArrayFloat("x");
        yf = region_record.asArrayFloat("y");

        // Convert to Double
        auto xf_size(xf.size());
        x.resize(xf_size);
        y.resize(xf_size);
        for (auto i = 0; i < xf_size; ++i) {
            x(i) = xf(i);
            y(i) = yf(i);
        }
    } else {
        x = region_record.asArrayDouble("x");
        y = region_record.asArrayDouble("y");
    }

    size_t npoints(x.size());
    if (region_name == "LCPolygon") {
        // Ignore last point, same as the first to enclose region but not in control points
        npoints -= 1;
    }

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        x -= 1.0;
        y -= 1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        for (auto i = 0; i < npoints; ++i) {
            control_points.push_back(casacore::Quantity(x(i), "pix"));
            control_points.push_back(casacore::Quantity(y(i), "pix"));
        }
        return true;
    }

    // Convert pixel coords to world coords
    size_t naxes(_image_shape.size());
    casacore::Matrix<casacore::Double> world_coords(naxes, x.size());
    casacore::Matrix<casacore::Double> pixel_coords(naxes, x.size());
    pixel_coords = 0.0;
    pixel_coords.row(0) = x;
    pixel_coords.row(1) = y;
    casacore::Vector<casacore::Bool> failures;

    try {
        if (_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
            // Make x and y world coord Vectors
            casacore::Vector<casacore::Double> x_world = world_coords.row(0);
            casacore::Vector<casacore::Double> y_world = world_coords.row(1);

            // Convert x and y Vectors to Quantities and add to control_points
            casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
            for (auto i = 0; i < npoints; ++i) {
                control_points.push_back(casacore::Quantity(x_world(i), world_units(0)));
                control_points.push_back(casacore::Quantity(y_world(i), world_units(1)));
            }
            return true;
        } else {
            return false;
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Export error: polygon Record conversion failed: {}", err.getMesg());
        return false;
    }
}

void RegionExport::ExportAnnCompassStyle(
    const CARTA::RegionStyle& region_style, const std::string& ann_coord_sys, std::string& region_line) {
    // Append compass labels and arrows to region line
    auto north_label = region_style.annotation_style().text_label0();
    auto east_label = region_style.annotation_style().text_label1();
    auto north_arrow = (region_style.annotation_style().is_north_arrow() ? "1" : "0");
    auto east_arrow = (region_style.annotation_style().is_east_arrow() ? "1" : "0");

    region_line += " compass=";
    if (!ann_coord_sys.empty()) {
        region_line += ann_coord_sys;
    }
    if (!north_label.empty()) {
        region_line += fmt::format(" {{{}}}", north_label);
    }
    if (!east_label.empty()) {
        region_line += fmt::format(" {{{}}}", east_label);
    }
    region_line += fmt::format(" {} {}", north_arrow, east_arrow);
}
