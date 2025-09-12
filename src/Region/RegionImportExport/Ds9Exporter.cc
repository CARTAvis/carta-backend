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

Ds9Exporter::Ds9Exporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape, bool export_pixels)
    : RegionExporter(coord_sys, shape), _export_pixels(export_pixels) {
    InitGlobalProperties();
    SetFileCoordFrame();
    _region_names = GetRegionTypeNames(CARTA::FileType::DS9_REG);
}

void Ds9Exporter::InitGlobalProperties() {
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

bool Ds9Exporter::AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
    auto region_type = region_state.type;
    std::vector<CARTA::Point> points = region_state.control_points;
    float rotation = region_state.rotation;

    if (region_type == CARTA::RegionType::ELLIPSE || region_type == CARTA::RegionType::ANNELLIPSE) {
        rotation += 90.0; // DS9 angle measured from x-axis
        if (rotation > 360.0) {
            rotation -= 360.0;
        }
    }

    float one_based_x = points[0].x() + 1; // Change from 0-based to 1-based image coordinate in x
    float one_based_y = points[0].y() + 1; // Change from 0-based to 1-based image coordinate in y
    std::string file_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y) or text(x, y) {Your Text Here}
            file_line = fmt::format("{}({:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            file_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", _region_names[region_type], one_based_x, one_based_y,
                points[1].x(), points[1].y(), rotation);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(cx,cy,width,height,angle)
            file_line = fmt::format(
                "# textbox({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", one_based_x, one_based_y, points[1].x(), points[1].y(), rotation);
            AddTextboxStyle(region_style, file_line);
            _file_lines.push_back(file_line);
            // # text(cx, cy)
            file_line = fmt::format("{}({:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y);
            if (rotation > 0.0) {
                file_line += fmt::format(" textangle={}", rotation);
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
                file_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f})", name, one_based_x, one_based_y, points[1].x());
            } else {
                if (rotation > 0.0) {
                    file_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f}, {})", _region_names[region_type], one_based_x, one_based_y,
                        points[1].x(), points[1].y(), rotation);
                } else {
                    file_line = fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y,
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
            file_line = fmt::format("{}({:.2f}, {:.2f}", _region_names[region_type], one_based_x, one_based_y);
            for (size_t i = 1; i < points.size(); ++i) {
                // Change from 0-based to 1-based image coordinate for the other points in (x, y)
                file_line += fmt::format(", {:.2f}, {:.2f}", points[i].x() + 1, points[i].y() + 1);
            }
            file_line += ")";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            // Add length and angle from x-axis in deg
            auto delta_x = points[1].x() - points[0].x();
            auto delta_y = points[1].y() - points[0].y();
            auto length = sqrt((delta_x * delta_x) + (delta_y * delta_y));
            auto angle = atan2((points[1].y() - points[0].y()), (points[1].x() - points[0].x())) * 180.0 / M_PI;
            file_line =
                fmt::format("{}({:.2f}, {:.2f}, {:.2f}, {:.2f})", _region_names[region_type], one_based_x, one_based_y, length, angle);
            break;
        }
        default:
            break;
    }

    // Add region style and add to list
    if (!file_line.empty()) {
        AddStyle(region_style, file_line);
        AddAnnotationStyle(region_type, region_style, file_line);
        file_line.append("\n");
        _file_lines.push_back(file_line);
        return true;
    }

    return false;
}

bool Ds9Exporter::AddRegion(const CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
    float rotation_deg = rotation.get("deg").getValue(); // from LCRegion "theta" value in radians

    std::string file_line;
    if (_export_pixels || _file_coord_frame == "image") {
        file_line = GetRegionPixelLine(region_type, control_points, rotation_deg, region_style);
    } else {
        file_line = GetRegionWorldLine(region_type, control_points, rotation_deg, region_style);
    }

    // Add region style and add to list
    if (!file_line.empty()) {
        AddStyle(region_style, file_line);
        AddAnnotationStyle(region_type, region_style, file_line);
        file_line.append("\n");
        _file_lines.push_back(file_line);
        return true;
    }

    return false;
}

bool Ds9Exporter::ExportRegions(const std::string& filename, std::string& error) {
    if (_file_lines.empty()) {
        error = "Export regions failed: no regions to export.";
        return false;
    }
    std::ofstream export_file(filename);
    for (auto& line : GetFileHeader()) {
        export_file << line;
    }
    for (auto& line : _file_lines) {
        export_file << line;
    }
    export_file.close();
    return true;
}

bool Ds9Exporter::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    if (_file_lines.empty()) {
        error = "Export regions failed: no regions to export.";
        return false;
    }

    for (auto& line : GetFileHeader()) {
        contents.push_back(line);
    }
    for (auto& line : _file_lines) {
        contents.push_back(line);
    }
    return true;
}

void Ds9Exporter::SetFileCoordFrame() {
    if (_export_pixels || _image_coord_frame.empty()) {
        _file_coord_frame = "image";
    } else if (_image_coord_frame == "b1950") {
        _file_coord_frame = "fk4";
    } else if (_image_coord_frame == "j2000") {
        _file_coord_frame = "fk5";
    } else {
        _file_coord_frame = _image_coord_frame;
    }
}

std::vector<std::string> Ds9Exporter::GetFileHeader() {
    std::ostringstream oss;
    oss << "# Region file format: DS9 CARTA " << VERSION_ID << std::endl;
    oss << "global";

    std::vector<std::string> ordered_keys = {
        "color", "dashlist", "width", "font", "select", "highlite", "dash", "fixed", "edit", "move", "delete", "include", "source"};
    for (auto& key : ordered_keys) {
        oss << " " << key << "=" << _global_properties[key];
    }
    oss << std::endl;

    std::string header = oss.str();
    std::vector<std::string> header_lines;
    header_lines.push_back(header);

    // Add coordinate frame
    oss.str("");
    oss << _file_coord_frame << std::endl;
    header_lines.push_back(oss.str());
    return header_lines;
}

std::string Ds9Exporter::GetRegionPixelLine(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    float rotation, const CARTA::RegionStyle& region_style) {
    std::string file_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y)
            file_line =
                fmt::format("{}({:.4f}, {:.4f})", _region_names[region_type], control_points[0].getValue(), control_points[1].getValue());
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            // box(x,y,width,height,angle)
            file_line = fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", _region_names[region_type], control_points[0].getValue(),
                control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue(), rotation);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(x,y,width,height)
            auto cx = control_points[0].getValue();
            auto cy = control_points[1].getValue();
            file_line = fmt::format("# textbox({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", cx, cy, control_points[2].getValue(),
                control_points[3].getValue(), rotation);
            AddTextboxStyle(region_style, file_line);
            // # text(x,y)
            file_line += fmt::format("{}({:.4f}, {:.4f})", _region_names[region_type], cx, cy);
            if (rotation > 0.0) {
                file_line += fmt::format(" textangle={}", rotation);
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
                file_line = fmt::format("{}({:.4f}, {:.4f}, {:.4f}\")", name, control_points[0].getValue(), control_points[1].getValue(),
                    control_points[2].getValue());
            } else {
                if (rotation == 0.0) {
                    file_line = fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f})", _region_names[region_type], control_points[0].getValue(),
                        control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue());
                } else {
                    file_line =
                        fmt::format("{}({:.4f}, {:.4f}, {:.4f}, {:.4f}, {})", _region_names[region_type], control_points[0].getValue(),
                            control_points[1].getValue(), control_points[2].getValue(), control_points[3].getValue(), rotation);
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
            file_line = fmt::format("{}({:.4f}", _region_names[region_type], control_points[0].getValue());
            for (size_t i = 1; i < control_points.size(); ++i) {
                file_line += fmt::format(", {:.4f}", control_points[i].getValue());
            }
            file_line += ")";
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

            file_line = fmt::format("{}({:.4f}, {:.4f}, {:.2f}, {:.2f})", _region_names[region_type], x0, y0, length, angle);
            break;
        }
        default:
            break;
    }

    return file_line;
}

std::string Ds9Exporter::GetRegionWorldLine(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    float rotation, const CARTA::RegionStyle& region_style) {
    std::string file_line;

    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // point(x, y)
            file_line = fmt::format("{}({:.9f}, {:.9f})", _region_names[region_type], control_points[0].get("deg").getValue(),
                control_points[1].get("deg").getValue());
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            // box(x,y,width,height,angle)
            casacore::Quantity cx(control_points[0]), cy(control_points[1]);
            casacore::Quantity width(control_points[2]), height(control_points[3]);
            file_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", _region_names[region_type], cx.get("deg").getValue(),
                cy.get("deg").getValue(), width.get("arcsec").getValue(), height.get("arcsec").getValue(), rotation);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            // # textbox(x,y,width,height,angle)
            casacore::Quantity cx(control_points[0]), cy(control_points[1]);
            casacore::Quantity width(control_points[2]), height(control_points[3]);
            file_line = fmt::format("# textbox({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", cx.get("deg").getValue(), cy.get("deg").getValue(),
                width.get("arcsec").getValue(), height.get("arcsec").getValue(), rotation);
            AddTextboxStyle(region_style, file_line);
            // # text(x,y)
            file_line += fmt::format("{}({:.9f}, {:.9f})", _region_names[region_type], cx.get("deg").getValue(), cy.get("deg").getValue());
            if (rotation > 0.0) {
                file_line += fmt::format(" textangle={}", rotation);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE: {
            // ellipse(x,y,radius,radius,angle) OR circle(x,y,radius)
            if (control_points[2].getValue() == control_points[3].getValue()) {
                // circle when bmaj==bmin
                std::string name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "# circle");
                file_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\")", name, control_points[0].get("deg").getValue(),
                    control_points[1].get("deg").getValue(), control_points[2].get("arcsec").getValue());
            } else {
                file_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f}\", {})", _region_names[region_type],
                    control_points[0].get("deg").getValue(), control_points[1].get("deg").getValue(),
                    control_points[2].get("arcsec").getValue(), control_points[3].get("arcsec").getValue(), rotation);
            }
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            // compass(x1,y1,length)
            file_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\")", _region_names[region_type], control_points[0].get("deg").getValue(),
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
            file_line = fmt::format("{}({:.9f}", _region_names[region_type], control_points[0].get("deg").getValue());
            for (size_t i = 1; i < control_points.size(); ++i) {
                file_line += fmt::format(", {:.9f}", control_points[i].get("deg").getValue());
            }
            file_line += ")";
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
                    file_line = fmt::format("{}({:.9f}, {:.9f}, {:.4f}\", {:.4f})", _region_names[region_type], x0, y0, length, angle);
                }
            }
            break;
        }
        default:
            break;
    }

    return file_line;
}

void Ds9Exporter::AddStyle(const CARTA::RegionStyle& region_style, std::string& file_line) {
    if (file_line[0] != '#') {
        file_line.append(" #");
    }
    file_line.append(" color=" + FormatColor(region_style.color()));
    file_line.append(" width=" + std::to_string(region_style.line_width()));

    bool is_text_region = file_line.find("text") != std::string::npos;
    bool region_has_font = is_text_region || file_line.find("compass") != std::string::npos;

    // Region name as 'text' if not text region, and font style for regions with font
    if (!region_style.name().empty() && !is_text_region) {
        file_line.append(" text={" + region_style.name() + "}");
        AddFontStyle(region_style, file_line);
    } else if (region_has_font) {
        AddFontStyle(region_style, file_line);
    }

    // dash list for enclosed regions
    if ((region_style.dash_list_size() > 0) && (region_style.dash_list(0) != 0)) {
        auto dash_on = region_style.dash_list(0);
        auto dash_off = region_style.dash_list_size() == 2 ? region_style.dash_list(1) : dash_on;
        auto dash_list = fmt::format(" dash=1 dashlist={} {}", dash_on, dash_off);
        file_line.append(dash_list);
    }
}

void Ds9Exporter::AddTextboxStyle(const CARTA::RegionStyle& region_style, std::string& file_line) {
    if (!region_style.name().empty()) {
        file_line += fmt::format(" text={{{}}}", region_style.name());
    }
    file_line += fmt::format(" align={}\n", text_positions[region_style.annotation_style().text_position()]);
}

void Ds9Exporter::AddFontStyle(const CARTA::RegionStyle& region_style, std::string& file_line) {
    if (!region_style.has_annotation_style()) {
        return;
    }

    // Use defaults if not in annotation style parameters
    auto font = region_style.annotation_style().font();
    if (font.empty()) {
        font = "helvetica";
    } else {
        std::transform(font.begin(), font.end(), font.begin(), ::tolower);
    }

    auto font_size = region_style.annotation_style().font_size();
    font_size = (font_size == 0 ? 10 : font_size);

    auto font_style = region_style.annotation_style().font_style();
    std::unordered_map<std::string, std::string> font_map = {{"", "normal roman"}, {"Normal", "normal roman"}, {"Bold", "bold roman"},
        {"Italic", "normal italic"}, {"Italic Bold", "bold italic"}};
    if (font_map.find(font_style) == font_map.end()) {
        font_style = "normal roman";
    } else {
        font_style = font_map[font_style];
    }

    file_line += fmt::format(" font=\"{} {} {}\"", font, font_size, font_style);
}

void Ds9Exporter::AddAnnotationStyle(CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& file_line) {
    if (!region_style.has_annotation_style()) {
        return;
    }

    switch (region_type) {
        case CARTA::RegionType::ANNPOINT: {
            AddAnnPointStyle(region_style, file_line);
            break;
        }
        case CARTA::RegionType::ANNLINE: {
            // line has no arrows
            file_line += " line=0 0";
            break;
        }
        case CARTA::RegionType::ANNVECTOR: {
            // by definition, vector has arrow
            file_line += " vector=1";
            break;
        }
        case CARTA::RegionType::ANNRULER: {
            std::string unit = (_image_coord_frame == "image" || _image_coord_frame == "linear" ? "image" : "degrees");
            file_line += fmt::format(" ruler={} {}", _image_coord_frame, unit);
            break;
        }
        case CARTA::RegionType::ANNTEXT: {
            file_line += fmt::format(" text={{{}}}", region_style.annotation_style().text_label0());
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            AddCompassStyle(region_style, _image_coord_frame, file_line);
            break;
        }
        default:
            return;
    }
}

void Ds9Exporter::AddAnnPointStyle(const CARTA::RegionStyle& region_style, std::string& file_line) {
    auto point_shape = region_style.annotation_style().point_shape();
    auto point_size = region_style.annotation_style().point_width();

    std::string shape("circle");
    bool fill(true);
    switch (point_shape) {
        case CARTA::PointAnnotationShape::SQUARE: {
            shape = "box";
            break;
        }
        case CARTA::PointAnnotationShape::BOX: {
            shape = "box";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::CIRCLE: {
            shape = "circle";
            break;
        }
        case CARTA::PointAnnotationShape::CIRCLE_LINED: {
            shape = "circle";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::DIAMOND: {
            shape = "diamond";
            break;
        }
        case CARTA::PointAnnotationShape::DIAMOND_LINED: {
            shape = "diamond";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::CROSS: {
            shape = "cross";
            fill = false;
            break;
        }
        case CARTA::PointAnnotationShape::X: {
            shape = "x";
            fill = false;
            break;
        }
        default: {
            break;
        }
    }

    file_line += fmt::format(" point={} {}", shape, point_size);

    if (fill) {
        file_line += " fill=1";
    }
}
