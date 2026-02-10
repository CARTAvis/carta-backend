/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RegionExporter.h"

#include <casacore/casa/OS/File.h>

#include "Logger/Logger.h"
#include "Util/Message.h"

using namespace carta;

RegionExporter::RegionExporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape)
    : _coord_sys(coord_sys), _image_shape(shape) {
    _image_coord_frame = GetImageDirectionFrame(coord_sys);
}

bool RegionExporter::CanExportToFile(const std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack) {
    // Check ability to create or overwrite region file if filename given.
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
        Message::ExportRegionAck(export_ack, false, error);
        export_ack.set_overwrite_confirmation_required(need_overwrite_confirmation);
        return false;
    }
    return true;
}

bool RegionExporter::AddRegion(int file_id, std::shared_ptr<Region> region, const CARTA::RegionStyle& region_style, bool export_pixels) {
    // Add file line for region using RegionState for pixel coords if reference image, else use region Record
    auto region_state = region->GetRegionState();
    bool region_added(false);

    if ((region_state.reference_file_id == file_id) && export_pixels) {
        region_added = AddRegion(region_state, region_style); // CRTF or DS9 exporter
    } else {
        try {
            // Convert region to another image, to world coordinates, or both
            // Get Record containing pixel coords of region in image with file_id
            casacore::TableRecord region_record = region->GetImageRegionRecord(file_id, _coord_sys, _image_shape);
            region_added = AddRegion(region_state, region_style, region_record, export_pixels);
        } catch (const casacore::AipsError& err) {
            spdlog::error("Add region failed: {}", err.getMesg());
        }
    }
    return region_added;
}

void RegionExporter::ExportRegions(const std::string& filename, std::string& message, CARTA::ExportRegionAck& export_ack) {
    // Export regions to file if filename is given, or to contents in ack message.  Complete message with success and message.
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
    Message::ExportRegionAck(export_ack, success, message);
}

bool RegionExporter::AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style,
    const casacore::RecordInterface& region_record, bool export_pixels) {
    // Convert casacore Record to pixel or world control points for region type, then add region file line.
    if (export_pixels) {
        casa::AnnotationBase::unitInit(); // enable "pix" unit
    }

    std::vector<casacore::Quantity> control_points;
    casacore::Quantity rotation(region_state.rotation, "deg");

    // Convert Record in pixel coordinates to control points; rotation updated for ellipse only
    bool converted(false);
    switch (region_state.type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT:
            converted = ConvertRecordToPoint(region_record, export_pixels, control_points);
            break;
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNTEXT:
            converted = ConvertRecordToRectangle(region_record, export_pixels, control_points);
            break;
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS:
            converted = ConvertRecordToEllipse(region_state, region_record, export_pixels, control_points, rotation);
            break;
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNVECTOR:
        case CARTA::RegionType::ANNRULER:
            converted = ConvertRecordToPolygonLine(region_record, export_pixels, control_points);
            break;
        default:
            break;
    }

    if (converted) {
        // Add file line for region file type
        return AddRegion(region_state.type, control_points, rotation, region_style); // CRTF or DS9 exporter
    }
    return converted;
}

bool RegionExporter::ConvertRecordToPoint(
    const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points) {
    // Point Record is a casacore LCBox with blc, trc arrays in pixel coordinates (blc = trc)
    casacore::Vector<casacore::Float> blc = region_record.asArrayFloat("blc");

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        blc -= (float)1.0;
    }

    if (export_pixels) {
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

bool RegionExporter::ConvertRecordToRectangle(
    const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points) {
    // Rectangle Record is a casacore LCPolygon in pixel coordinates with blc, brc, trc, tlc in x and y arrays.
    // A rotated rectangle Record is for the unrotated box.
    casacore::Vector<casacore::Double> x, y;

    if (region_record.dataType("x") == casacore::TpArrayFloat) {
        // Convert Float to Double
        casacore::Vector<casacore::Float> xf = region_record.asArrayFloat("x");
        casacore::Vector<casacore::Float> yf = region_record.asArrayFloat("y");
        x = FloatVectorToDouble(xf);
        y = FloatVectorToDouble(yf);
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

    if (export_pixels) {
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

bool RegionExporter::ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record,
    bool export_pixels, std::vector<casacore::Quantity>& control_points, casacore::Quantity& rotation) {
    // Ellipse Record is a casacore LCEllipsoid with center and radii in pixel coordinates, and theta for angle.
    // Use RegionState to check if bmaj/bmin swapped so bmaj > bmin.
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

    if (reversed) {
        // Theta is the angle from the x-axis (east) to the major axis of the ellipse.
        // Carta rotation is from north.
        rotation += 90.0;
        if (rotation.getValue() > 360.0) {
            rotation -= 360.0;
        }
    }

    if (export_pixels) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(center(0), "pix"));
        control_points.push_back(casacore::Quantity(center(1), "pix"));

        // Restore original axes order
        if (reversed) {
            control_points.push_back(casacore::Quantity(radii(1), "pix"));
            control_points.push_back(casacore::Quantity(radii(0), "pix"));
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

bool RegionExporter::ConvertRecordToPolygonLine(
    const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points) {
    // Polygon Record is a casacore LCPolygon in pixel coordinates with points in x, y arrays.
    casacore::String region_name = region_record.asString("name");
    casacore::Vector<casacore::Double> x, y;

    if (region_record.dataType("x") == casacore::TpArrayFloat) {
        // Convert Float to Double
        casacore::Vector<casacore::Float> xf = region_record.asArrayFloat("x");
        casacore::Vector<casacore::Float> yf = region_record.asArrayFloat("y");
        x = FloatVectorToDouble(xf);
        y = FloatVectorToDouble(yf);
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

    if (export_pixels) {
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

casacore::Vector<casacore::Double> RegionExporter::FloatVectorToDouble(const casacore::Vector<casacore::Float>& float_vector) {
    casacore::Vector<casacore::Double> double_vector;
    auto float_size(float_vector.size());
    double_vector.resize(float_size);
    for (size_t i = 0; i < float_size; ++i) {
        double_vector(i) = float_vector(i);
    }
    return double_vector;
}

void RegionExporter::AddCompassStyle(const CARTA::RegionStyle& region_style, const std::string& coord_frame, std::string& file_line) {
    // Append compass coordinate, labels and arrows to region line
    auto north_label = region_style.annotation_style().text_label0();
    auto east_label = region_style.annotation_style().text_label1();
    auto north_arrow = (region_style.annotation_style().is_north_arrow() ? "1" : "0");
    auto east_arrow = (region_style.annotation_style().is_east_arrow() ? "1" : "0");

    file_line += " compass=";
    if (!coord_frame.empty()) {
        file_line += coord_frame;
    }
    if (!north_label.empty()) {
        file_line += fmt::format(" {{{}}}", north_label);
    }
    if (!east_label.empty()) {
        file_line += fmt::format(" {{{}}}", east_label);
    }
    file_line += fmt::format(" {} {}", north_arrow, east_arrow);
}
