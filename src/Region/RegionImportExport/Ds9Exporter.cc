/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Exporter.cc: export regions in DS9 format

#include "Ds9Exporter.h"

#include <spdlog/fmt/fmt.h>

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#include "Util/App.h"

#define REGION_COLOR "#2EE6D6"
#define REGION_LINE_WIDTH 2

using namespace carta;

Ds9Exporter::Ds9Exporter(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coords)
    : RegionExporter(image_coord_sys, image_shape), _pixel_coords(pixel_coords) {
    // Export regions to DS9 format
    // Set properties for file header
    InitGlobalProperties();
    _region_names = GetRegionTypeNames(CARTA::FileType::DS9_REG);

    // Multiple options for these image frames, use fk* version
    SetImageReferenceFrame(); // casacore frame, from coordinate system
    if (_image_ref_frame == "b1950") {
        _image_ref_frame = "fk4";
    } else if (_image_ref_frame == "j2000") {
        _image_ref_frame = "fk5";
    }

    if (pixel_coords) {
        _file_ref_frame = "image";
    } else {
        _file_ref_frame = _image_ref_frame;
    }

    AddHeader();
}

void Ds9Exporter::InitGlobalProperties() {
    // Set global properties to defaults
    _global_properties["color"] = "green";
    _global_properties["dashlist"] = "8 3";
    _global_properties["width"] = "1";
    _global_properties["font"] = "\"helvetica 10 normal roman\"";
    _global_properties["select"] = "1";
    _global_properties["highlite"] = "1";
    _global_properties["dash"] = "0";
    _global_properties["fixed"] = "0";
    _global_properties["edit"] = "1";
    _global_properties["move"] = "1";
    _global_properties["delete"] = "1";
    _global_properties["include"] = "1";
    _global_properties["source"] = "1";
}

// Public: for exporting regions

bool Ds9Exporter::AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
    // Add pixel-coord region using RegionState
    auto region_type = region_state.type;
    std::vector<CARTA::Point> points = region_state.control_points;
    float angle = region_state.rotation;
    if (region_type == CARTA::RegionType::ELLIPSE || region_type == CARTA::RegionType::ANNELLIPSE) {
        angle += 90.0; // DS9 angle measured from x-axis
        if (angle > 360.0) {
            angle -= 360.0;
        }
    }

    float one_based_x = points[0].x() + 1; // Change from 0-based to 1-based image coordinate in x
    float one_based_y = points[0].y() + 1; // Change from 0-based to 1-based image coordinate in y
    std::string region_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y) or text(x, y) {Your Text Here}
            region_line = fmt::format("{}({:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            region_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", _region_names[region_type], one_based_x, one_based_y,
                points[1].x(), points[1].y(), angle);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(cx,cy,width,height,angle)
            region_line =
                fmt::format("# textbox({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", one_based_x, one_based_y, points[1].x(), points[1].y(), angle);
            ExportTextboxStyleParameters(region_style, region_line);
            _export_regions.push_back(region_line);
            // # text(cx, cy)
            region_line = fmt::format("{}({:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y);
            if (angle > 0.0) {
                region_line += fmt::format(" textangle={}", angle);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS: {
            // ellipse(x,y,radius,radius,angle) OR circle(x,y,radius) OR compass(x,y,length)
            if (points[1].x() == points[1].y()) { // bmaj == bmin or compass length == length
                std::string name = _region_names[region_type];
                if (region_type != CARTA::RegionType::ANNCOMPASS) {
                    name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "# circle");
                }
                region_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f})", name, one_based_x, one_based_y, points[1].x());
            } else {
                if (angle > 0.0) {
                    region_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", _region_names[region_type], one_based_x,
                        one_based_y, points[1].x(), points[1].y(), angle);
                } else {
                    region_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y,
                        points[1].x(), points[1].y());
                }
            }
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNRULER: {
            // polygon(x1,y1,x2,y2,x3,y3,...)
            region_line = fmt::format("{}({:.2f}, {:.2f}", _region_names[region_type], one_based_x, one_based_y);
            for (size_t i = 1; i < points.size(); ++i) {
                // Change from 0-based to 1-based image coordinate for the other points in (x, y)
                region_line += fmt::format(", {:.2f}, {:.2f}", points[i].x() + 1, points[i].y() + 1);
            }
            region_line += ")";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            // Add length and angle from x-axis in deg
            auto delta_x = points[1].x() - points[0].x();
            auto delta_y = points[1].y() - points[0].y();
            auto length = sqrt((delta_x * delta_x) + (delta_y * delta_y));
            auto angle = atan2((points[1].y() - points[0].y()), (points[1].x() - points[0].x())) * 180.0 / M_PI;
            region_line =
                fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y, length, angle);
            break;
        }
        default:
            break;
    }

    // Add region style and add to list
    if (!region_line.empty()) {
        ExportStyleParameters(region_style, region_line);
        ExportAnnotationStyleParameters(region_type, region_style, region_line);
        region_line.append("\n");
        _export_regions.push_back(region_line);
        return true;
    }

    return false;
}

bool Ds9Exporter::AddExportRegion(const CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
    // Add region using Quantities
    float angle = rotation.get("deg").getValue(); // from LCRegion "theta" value in radians

    std::string region_line;
    if (_pixel_coords || _file_ref_frame.empty()) {
        region_line = AddExportRegionPixel(region_type, control_points, angle, region_style);
    } else {
        region_line = AddExportRegionWorld(region_type, control_points, angle, region_style);
    }

    // Add region style and add to list
    if (!region_line.empty()) {
        ExportStyleParameters(region_style, region_line);
        ExportAnnotationStyleParameters(region_type, region_style, region_line);
        region_line.append("\n");
        _export_regions.push_back(region_line);
        return true;
    }

    return false;
}

bool Ds9Exporter::ExportRegions(const std::string& filename, std::string& error) {
    // Print regions to DS9 file
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    std::ofstream export_file(filename);
    for (auto& region : _export_regions) {
        export_file << region;
    }
    export_file.close();
    return true;
}

bool Ds9Exporter::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    // Print regions to DS9 file lines in vector
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    contents = _export_regions;
    return true;
}

void Ds9Exporter::SetImageReferenceFrame() {
    // Set image coord sys direction frame
    if (!_coord_sys) {
        return;
    }

    if (_coord_sys->hasDirectionCoordinate()) {
        casacore::MDirection::Types reference_frame = _coord_sys->directionCoordinate().directionType();
        _image_ref_frame = casacore::MDirection::showType(reference_frame);
    } else if (_coord_sys->hasLinearCoordinate()) {
        _image_ref_frame = "linear";
    } else {
        _image_ref_frame = "image";
    }
}

void Ds9Exporter::AddHeader() {
    // print file format, globals, and coord sys
    std::ostringstream os;
    os << "# Region file format: DS9 CARTA " << VERSION_ID << std::endl;
    os << "global";

    std::vector<std::string> ordered_keys = {
        "color", "dashlist", "width", "font", "select", "highlite", "dash", "fixed", "edit", "move", "delete", "include", "source"};
    for (auto& key : ordered_keys) {
        os << " " << key << "=" << _global_properties[key];
    }
    os << std::endl;

    std::string header = os.str();
    _export_regions.push_back(header);

    // Add coordinate frame
    os.str("");
    if (_file_ref_frame.empty()) {
        os << "image\n";
    } else {
        os << _file_ref_frame << std::endl;
    }
    _export_regions.push_back(os.str());
}

std::string Ds9Exporter::AddExportRegionPixel(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    float angle, const CARTA::RegionStyle& region_style) {
    // Add region using pixel Quantities.  RegionStyle needed for 2-line text region.
    std::string region_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y)
            region_line =
                fmt::format("{}({:.4f}, {:.4f})", _region_names[region_type], control_points[0].getValue(), control_points[1].getValue());
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            // box(x,y,width,height,angle)
            region_line = fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", _region_names[region_type], control_points[0].getValue(),
                control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue(), angle);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(x,y,width,height)
            auto cx = control_points[0].getValue();
            auto cy = control_points[1].getValue();
            region_line = fmt::format(
                "# textbox({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", cx, cy, control_points[2].getValue(), control_points[3].getValue(), angle);
            ExportTextboxStyleParameters(region_style, region_line);
            // # text(x,y)
            region_line += fmt::format("{}({:.4f}, {:.4f})", _region_names[region_type], cx, cy);
            if (angle > 0.0) {
                region_line += fmt::format(" textangle={}", angle);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS: {
            // ellipse(x,y,radius,radius,angle) OR circle(x,y,radius) OR compass(x,y,length)
            if (control_points[2].getValue() == control_points[3].getValue()) { // bmaj == bmin
                std::string name = _region_names[region_type];
                if (region_type != CARTA::RegionType::ANNCOMPASS) {
                    name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "# circle");
                }
                region_line = fmt::format("{}({:.4f}, {:.4f}, {:.4f}\")", name, control_points[0].getValue(), control_points[1].getValue(),
                    control_points[2].getValue());
            } else {
                if (angle == 0.0) {
                    region_line =
                        fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f})", _region_names[region_type], control_points[0].getValue(),
                            control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue());
                } else {
                    region_line =
                        fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", _region_names[region_type], control_points[0].getValue(),
                            control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue(), angle);
                }
            }
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNRULER: {
            // polygon(x1,y1,x2,y2,x3,y3,...)
            region_line = fmt::format("{}({:.4f}", _region_names[region_type], control_points[0].getValue());
            for (size_t i = 1; i < control_points.size(); ++i) {
                region_line += fmt::format(", {:.4f}", control_points[i].getValue());
            }
            region_line += ")";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            double x0(control_points[0].getValue()), y0(control_points[1].getValue());
            double x1(control_points[2].getValue()), y1(control_points[3].getValue());
            auto delta_x = x1 - x0;
            auto delta_y = y1 - y0;
            auto length = sqrt((delta_x * delta_x) + (delta_y * delta_y));

            // Angle from x-axis
            auto angle = atan2((y1 - y0), (x1 - x0)) * 180.0 / M_PI;

            region_line = fmt::format("{}({:.4f}, {:.4f}, {:.2f}, {:.2f})", _region_names[region_type], x0, y0, length, angle);
            break;
        }
        default:
            break;
    }

    return region_line;
}

std::string Ds9Exporter::AddExportRegionWorld(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    float angle, const CARTA::RegionStyle& region_style) {
    // Add region using world Quantities.  RegionStyle needed for 2-line text region.
    std::string region_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y)
            region_line = fmt::format("{}({:.9f}, {:.9f})", _region_names[region_type], control_points[0].get("deg").getValue(),
                control_points[1].get("deg").getValue());
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            // box(x,y,width,height,angle)
            casacore::Quantity cx(control_points[0]), cy(control_points[1]);
            casacore::Quantity width(control_points[2]), height(control_points[3]);
            region_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", _region_names[region_type], cx.get("deg").getValue(),
                cy.get("deg").getValue(), width.get("arcsec").getValue(), height.get("arcsec").getValue(), angle);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(x,y,width,height,angle)
            casacore::Quantity cx(control_points[0]), cy(control_points[1]);
            casacore::Quantity width(control_points[2]), height(control_points[3]);
            region_line = fmt::format("# textbox({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", cx.get("deg").getValue(),
                cy.get("deg").getValue(), width.get("arcsec").getValue(), height.get("arcsec").getValue(), angle);
            ExportTextboxStyleParameters(region_style, region_line);
            // # text(x,y)
            region_line +=
                fmt::format("{}({:.9f}, {:.9f})", _region_names[region_type], cx.get("deg").getValue(), cy.get("deg").getValue());
            if (angle > 0.0) {
                region_line += fmt::format(" textangle={}", angle);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE: {
            // ellipse(x,y,radius,radius,angle) OR circle(x,y,radius)
            if (control_points[2].getValue() == control_points[3].getValue()) {
                // circle when bmaj==bmin
                std::string name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "# circle");
                region_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\")", name, control_points[0].get("deg").getValue(),
                    control_points[1].get("deg").getValue(), control_points[2].get("arcsec").getValue());
            } else {
                region_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", _region_names[region_type],
                    control_points[0].get("deg").getValue(), control_points[1].get("deg").getValue(),
                    control_points[2].get("arcsec").getValue(), control_points[3].get("arcsec").getValue(), angle);
            }
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            // compass(x1,y1,length)
            region_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\")", _region_names[region_type], control_points[0].get("deg").getValue(),
                control_points[1].get("deg").getValue(), control_points[2].get("arcsec").getValue());
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNRULER: {
            // region_name(x1,y1,x2,y2,...)
            region_line = fmt::format("{}({:.9f}", _region_names[region_type], control_points[0].get("deg").getValue());
            for (size_t i = 1; i < control_points.size(); ++i) {
                region_line += fmt::format(", {:.9f}", control_points[i].get("deg").getValue());
            }
            region_line += ")";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            // vector(x,y,length,angle)
            // x,y
            double x0(control_points[0].get("deg").getValue()), y0(control_points[1].get("deg").getValue());

            // length, angle from pixel coords
            if (_coord_sys->hasDirectionCoordinate()) {
                std::string dir_frame;
                std::vector<casacore::Quantity> point0{control_points[0], control_points[1]};
                std::vector<casacore::Quantity> point1{control_points[2], control_points[3]};
                casacore::Vector<casacore::Double> point0_pix, point1_pix;
                if (ConvertPointToPixels(_coord_sys, dir_frame, point0, point0_pix) &&
                    ConvertPointToPixels(_coord_sys, dir_frame, point1, point1_pix)) {
                    // World points as casacore::MVDirection to get separation
                    auto mvdir0 = _coord_sys->directionCoordinate().toWorld(point0_pix);
                    auto mvdir1 = _coord_sys->directionCoordinate().toWorld(point1_pix);
                    auto length = mvdir0.separation(mvdir1, "arcsec").getValue();
                    auto angle = atan2((point1_pix[1] - point0_pix[1]), (point1_pix[0] - point0_pix[0])) * 180.0 / M_PI;
                    region_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f})", _region_names[region_type], x0, y0, length, angle);
                }
            }
            break;
        }
        default:
            break;
    }

    return region_line;
}

void Ds9Exporter::ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    // Add common region style properties from RegionStyle to line string
    if (region_line[0] != '#') {
        region_line.append(" #");
    }
    region_line.append(" color=" + FormatColor(region_style.color()));
    region_line.append(" width=" + std::to_string(region_style.line_width()));

    bool is_text_region = region_line.find("text") != std::string::npos;
    bool region_has_font = is_text_region || region_line.find("compass") != std::string::npos;
    if (!region_style.name().empty() && !is_text_region) {
        region_line.append(" text={" + region_style.name() + "}");
        ExportFontParameters(region_style, region_line);
    } else if (region_has_font) {
        ExportFontParameters(region_style, region_line);
    }

    // dash list for enclosed regions
    if ((region_style.dash_list_size() > 0) && (region_style.dash_list(0) != 0)) {
        auto dash_on = region_style.dash_list(0);
        auto dash_off = region_style.dash_list_size() == 2 ? region_style.dash_list(1) : dash_on;
        auto dash_list = fmt::format(" dash=1 dashlist={} {}", dash_on, dash_off);
        region_line.append(dash_list);
    }
}

void Ds9Exporter::ExportTextboxStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    // Add region name and alignment
    if (!region_style.name().empty()) {
        region_line += fmt::format(" text={{{}}}", region_style.name());
    }
    region_line += fmt::format(" align={}\n", text_positions[region_style.annotation_style().text_position()]);
}

void Ds9Exporter::ExportFontParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    if (!region_style.has_annotation_style()) {
        return;
    }

    auto font = region_style.annotation_style().font();
    if (font.empty()) {
        font = "helvetica";
    } else {
        std::transform(font.begin(), font.end(), font.begin(), ::tolower);
    }

    auto font_size = region_style.annotation_style().font_size();
    if (font_size == 0) {
        font_size = 10;
    }

    auto font_style = region_style.annotation_style().font_style();
    std::unordered_map<std::string, std::string> font_map = {{"", "normal roman"}, {"Normal", "normal roman"}, {"Bold", "bold roman"},
        {"Italic", "normal italic"}, {"Italic Bold", "bold italic"}};
    if (font_map.find(font_style) == font_map.end()) {
        font_style = "normal roman";
    } else {
        font_style = font_map[font_style];
    }

    region_line += fmt::format(" font=\"{} {} {}\"", font, font_size, font_style);
}

void Ds9Exporter::ExportAnnotationStyleParameters(
    CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& region_line) {
    if (!region_style.has_annotation_style()) {
        return;
    }

    switch (region_type) {
        case CARTA::RegionType::ANNPOINT: {
            ExportAnnPointParameters(region_style, region_line);
            break;
        }
        case CARTA::RegionType::ANNLINE: {
            // line has no arrows
            region_line += " line=0 0";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            // by definition, vector has arrow
            region_line += " vector=1";
            break;
        }
        case CARTA::RegionType::ANNRULER: {
            std::string unit = (_image_ref_frame == "image" || _image_ref_frame == "linear" ? "image" : "degrees");
            region_line += fmt::format(" ruler={} {}", _image_ref_frame, unit);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            region_line += fmt::format(" text={{{}}}", region_style.annotation_style().text_label0());
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            ExportAnnCompassStyle(region_style, _image_ref_frame, region_line);
            break;
        }
        default:
            return;
    }
}

void Ds9Exporter::ExportAnnPointParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    std::string point_shape("circle");
    bool fill(true);

    switch (region_style.annotation_style().point_shape()) {
        case CARTA::PointAnnotationShape::SQUARE: {
            point_shape = "box";
            break;
        }
        case CARTA::PointAnnotationShape::BOX: {
            point_shape = "box";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::CIRCLE: {
            point_shape = "circle";
            break;
        }
        case CARTA::PointAnnotationShape::CIRCLE_LINED: {
            point_shape = "circle";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::DIAMOND: {
            point_shape = "diamond";
            break;
        }
        case CARTA::PointAnnotationShape::DIAMOND_LINED: {
            point_shape = "diamond";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::CROSS: {
            point_shape = "cross";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::X: {
            point_shape = "x";
            fill = false;
            break;
        }
        default: {
            point_shape = "box";
            break;
        }
    }

    auto point_size = region_style.annotation_style().point_width();
    region_line += fmt::format(" point={} {}", point_shape, point_size);

    if (fill) {
        region_line += " fill=1";
    }
}
