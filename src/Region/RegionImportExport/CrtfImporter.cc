/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfImporter.cc: import regions from CRTF file or contents string

#include "CrtfImporter.h"

#include <imageanalysis/Annotations/AnnotationBase.h>

#include "Logger/Logger.h"

using namespace carta;

CrtfImporter::CrtfImporter(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename)
    : RegionImporter(image_coord_sys, file_id) {
    SetParserDelim(" ,[]");
    _region_names = GetRegionTypeNames(CARTA::FileType::CRTF);

    try {
        std::vector<std::string> file_lines = ReadRegionFile(file, file_is_filename);
        SetFileLineRegions(file_lines);
    } catch (const casacore::AipsError& err) {
        // Note exception and quit.
        casacore::String error = err.getMesg().before("at File");
        error = error.before("thrown by");
        _errors = error;
    }
}

void CrtfImporter::SetFileLineRegions(std::vector<std::string>& file_lines) {
    casa::AnnotationBase::unitInit(); // enable "pix" unit
    bool is_combo_region(false);      // true for textbox + text
    RegionProperties region_properties;

    for (auto& line : file_lines) {
        // skip blank line and comment (check for non-CRTF carta region)
        if (line.empty() || IsCommentLine(line)) {
            continue;
        }

        // Parse line
        std::vector<std::string> parameters;
        std::unordered_map<std::string, std::string> properties;
        ParseRegionParameters(line, parameters, properties);

        // Coordinate frame for world coordinates conversion
        RegionState region_state;
        CARTA::RegionStyle region_style;
        auto region = parameters[0] == "ann" ? parameters[1] : parameters[0];
        auto coord_frame = GetRegionDirectionFrame(properties);

        if (region == "symbol") {
            region_state = ImportAnnSymbolText(parameters, coord_frame);
        } else if ((region == "line") || (region == "vector") || (region == "ruler")) {
            region_state = ImportAnnPoly(parameters, coord_frame);
        } else if (region.find("box") != std::string::npos) { // "box", "centerbox", "rotbox", "textbox"
            is_combo_region = (region == "textbox");
            region_state = ImportAnnBox(parameters, coord_frame);
        } else if ((region == "ellipse") || (region == "circle") || (region == "compass")) {
            region_state = ImportAnnEllipse(parameters, coord_frame);
        } else if (region.find("poly") != std::string::npos) { // "poly(gon)", "polyline"
            region_state = ImportAnnPoly(parameters, coord_frame);
        } else if (region == "text") {
            if (is_combo_region) {
                region_state.type = CARTA::RegionType::ANNTEXT;
            } else {
                // only get text control points if no textbox already defined
                region_state = ImportAnnSymbolText(parameters, coord_frame);
            }
        } else if (region == "global") {
            _global_properties = properties;
        } else {
            _errors.append(region + " not supported.\n");
        }

        if (region_state.RegionDefined() || is_combo_region) {
            // Set RegionStyle
            auto region_type = region_state.type;
            region_style = ImportStyle(region_type, properties);

            // Set AnnotationStyle fields for some regions
            switch (region_type) {
                case CARTA::RegionType::ANNPOINT: {
                    // Add point shape, size
                    auto symbol_char = parameters[parameters.size() - 1]; // [ann], symbol, x, y, char
                    ImportPointStyle(symbol_char, properties, region_style.mutable_annotation_style());
                    break;
                }
                case CARTA::RegionType::ANNTEXT: {
                    if (region == "text") {
                        // Set text label for text region
                        if (parameters.size() == 4) { // text, x, y, "text"
                            std::string label(parameters[3]);
                            if (!label.empty() &&
                                ((label.front() == '"' && label.back() == '"') || (label.front() == '\'' && label.back() == '\''))) {
                                label.pop_back();
                                label = label.substr(1);
                            }
                            region_style.mutable_annotation_style()->set_text_label0(label);
                        }
                    } else {
                        // Set text position for textbox region
                        std::string align = GetProperty("align", properties);
                        region_style.mutable_annotation_style()->set_text_position(GetTextPosition(align));
                    }
                    break;
                }
                case CARTA::RegionType::ANNCOMPASS: {
                    std::string compass_properties = GetProperty("compass", properties);
                    if (!compass_properties.empty()) {
                        std::string coordinate_system; // same as "coord" property, not needed for CRTF
                        ImportCompassStyle(compass_properties, coordinate_system, region_style.mutable_annotation_style());
                    }
                    break;
                }
                default:
                    break;
            }

            // Set RegionProperties and add to list
            if (is_combo_region && region == "text") {
                // Reset flag and add text style to (hopefully) previously defined textbox
                is_combo_region = false;
                if (region_properties.state.RegionDefined()) {
                    AddTextStyleToProperties(region_style, region_properties);
                }
            } else {
                // Set new region properties
                region_properties = RegionProperties(region_state, region_style);
            }

            if (!is_combo_region && region_properties.state.RegionDefined()) {
                _regions.push_back(region_properties);
            }
        }
    }
}

std::string CrtfImporter::GetRegionDirectionFrame(const std::unordered_map<std::string, std::string>& properties) {
    std::string dir_frame = GetProperty("coord", properties, true);
    if (dir_frame.empty()) {
        dir_frame = GetImageDirectionFrame(_coord_sys); // use direction frame from image coord sys
    }
    return dir_frame;
}

RegionState CrtfImporter::ImportAnnSymbolText(std::vector<std::string>& parameters, std::string& coord_frame) {
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    // (ann) symbol x y
    // text x y
    // optional symbol shape or text string
    if (parameters.size() >= 3) {
        // Convert string to Quantities
        casacore::Quantity x, y;
        try {
            casacore::readQuantity(x, parameters[param_index++]);
            casacore::readQuantity(y, parameters[param_index++]);
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import Quantity error: {}", region, err.getMesg());
            _errors.append(region + " parameters invalid.\n");
            return region_state;
        }

        try {
            // Convert to pixels
            std::vector<casacore::Quantity> point;
            point.push_back(x);
            point.push_back(y);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, coord_frame, point, pixel_coords)) {
                // Set control points
                std::vector<CARTA::Point> control_points;
                control_points.push_back(Message::Point(pixel_coords));

                // Set RegionState
                CARTA::RegionType type;
                if (region == "symbol") {
                    type = is_annotation ? CARTA::ANNPOINT : CARTA::POINT;
                } else if (region == "text") {
                    type = CARTA::ANNTEXT;
                    // Set width/height of textbox to 0 for dynamic sizing in frontend
                    control_points.push_back(Message::Point(0.0, 0.0));
                } else {
                    spdlog::error("Unknown region {} import failed", region);
                    _errors.append("Unknown region " + region + " import failed.\n");
                    return region_state;
                }

                float rotation(0.0);
                region_state = RegionState(_file_id, type, control_points, rotation);
            } else {
                spdlog::error("{} import conversion to pixel failed", region);
                _errors.append(region + " import failed.\n");
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import error: {}", region, err.getMesg());
            _errors.append(region + " import failed.\n");
        }
    } else {
        _errors.append(region + " syntax invalid.\n");
    }
    return region_state;
}

RegionState CrtfImporter::ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame) {
    RegionState region_state;

    // box blcx blcy trcx trcy
    // centerbox cx cy width height
    // rotbox cx cy width height angle
    // textbox cx cy width height angle
    if (parameters.size() >= 5) {
        CARTA::RegionType type;
        auto first_param = parameters[0];
        if (first_param == "ann") {
            type = CARTA::RegionType::ANNRECTANGLE;
        } else if (first_param == "textbox") {
            type = CARTA::RegionType::ANNTEXT;
        } else {
            type = CARTA::RegionType::RECTANGLE;
        }

        // Use parameters to get control points and rotation
        std::vector<CARTA::Point> control_points;
        float rotation(0.0);
        if (!GetBoxControlPoints(parameters, coord_frame, control_points, rotation)) {
            return region_state;
        }

        // Create RegionState and add to vector
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _errors.append("box syntax invalid.\n");
    }

    return region_state;
}

RegionState CrtfImporter::ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame) {
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    // ellipse cx cy bmaj bmin angle
    // circle cx cy r
    if (parameters.size() >= 4) {
        casacore::Quantity cx, cy, p3, p4, p5;
        float rotation(0.0);
        try {
            // Center point
            casacore::readQuantity(cx, parameters[param_index++]);
            casacore::readQuantity(cy, parameters[param_index++]);
            casacore::readQuantity(p3, parameters[param_index++]);

            if (region == "ellipse") {
                casacore::readQuantity(p4, parameters[param_index++]);

                // rotation
                casacore::readQuantity(p5, parameters[param_index]);
                rotation = p5.get("deg").getValue();
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import Quantity error: {}", region, err.getMesg());
            _errors.append(region + " parameters invalid.\n");
        }

        try {
            // Convert to pixels
            std::vector<casacore::Quantity> point;
            point.push_back(cx);
            point.push_back(cy);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, coord_frame, point, pixel_coords)) {
                // Set control points for center point
                std::vector<CARTA::Point> control_points;
                control_points.push_back(Message::Point(pixel_coords));

                // Set bmaj, bmin or radius
                if (region == "ellipse") {
                    control_points.push_back(Message::Point(WorldToPixelLength(p3, 0), WorldToPixelLength(p4, 1)));
                } else {
                    double radius = WorldToPixelLength(p3, 0);
                    control_points.push_back(Message::Point(radius, radius));
                }

                // Create RegionState and add to vector
                CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNELLIPSE : CARTA::RegionType::ELLIPSE);
                if (region == "compass") {
                    type = CARTA::RegionType::ANNCOMPASS;
                }

                region_state = RegionState(_file_id, type, control_points, rotation);
            } else {
                spdlog::error("{} import conversion to pixel failed", region);
                _errors.append(region + " import failed.\n");
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import error: {}", region, err.getMesg());
            _errors.append(region + " import failed.\n");
        }
    } else {
        _errors.append(region + " syntax invalid.\n");
    }

    return region_state;
}

RegionState CrtfImporter::ImportAnnPoly(std::vector<std::string>& parameters, std::string& coord_frame) {
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    // (ann) poly x1 y1 x2 y2 x3 y3 ...
    // (ann) polyline x1 y1 x2 y2 x3 y3...
    // (ann) line x1 y1 x2 y2
    // vector x1 y1 x2 y2
    // ruler x1 y1 x2 y2
    if (parameters.size() >= 5) {
        // Check: poly at least 3 points, line etc two points
        if (region.find("poly") == 0) {
            if (parameters.size() < 7) {
                _errors.append(region + " syntax invalid.\n");
                return region_state;
            }
        } else if (parameters.size() < 5) {
            _errors.append(region + " syntax invalid.\n");
            return region_state;
        }

        try {
            std::vector<CARTA::Point> control_points;

            // Convert parameters in x,y pairs
            for (size_t i = param_index; i < parameters.size(); i += 2) {
                casacore::Quantity x, y;
                casacore::readQuantity(x, parameters[i]);
                casacore::readQuantity(y, parameters[i + 1]);

                // Convert to pixels
                std::vector<casacore::Quantity> point;
                point.push_back(x);
                point.push_back(y);
                casacore::Vector<casacore::Double> pixel_coords;
                if (ConvertPointToPixels(_coord_sys, coord_frame, point, pixel_coords)) {
                    // Set control points
                    control_points.push_back(Message::Point(pixel_coords));
                } else {
                    spdlog::error("{} import conversion to pixel failed", region);
                    _errors.append(region + " import failed.\n");
                    return region_state;
                }
            }

            // Set type
            CARTA::RegionType type;
            if (region == "poly" || region == "polygon") {
                type = is_annotation ? CARTA::ANNPOLYGON : CARTA::POLYGON;
            } else if (region == "polyline") {
                type = is_annotation ? CARTA::ANNPOLYLINE : CARTA::POLYLINE;
            } else if (region == "line") {
                type = is_annotation ? CARTA::ANNLINE : CARTA::LINE;
            } else if (region == "vector") {
                type = CARTA::ANNVECTOR;
            } else if (region == "ruler") {
                type = CARTA::ANNRULER;
            } else {
                spdlog::error("Unknown region {} import failed", region);
                _errors.append("Unknown region " + region + " import failed.\n");
                return region_state;
            }

            // Create RegionState and add to vector
            float rotation(0.0);
            region_state = RegionState(_file_id, type, control_points, rotation);
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import error: {}", region, err.getMesg());
            _errors.append(region + " import failed.\n");
        }
    } else {
        _errors.append(region + " syntax invalid.\n");
    }

    return region_state;
}

CARTA::RegionStyle CrtfImporter::ImportStyle(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties) {
    CARTA::RegionStyle region_style;

    // Set name
    auto name = GetProperty("name", properties);
    if (!name.empty()) {
        if (name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.length() - 2);
        }
        region_style.set_name(name);
    }

    // Set color
    auto color = GetProperty("color", properties, true);
    if (color.empty()) {
        color = "green"; // CRTF default
    } else {
        color = FormatColor(color);
    }
    if (std::strtoul(color.c_str(), nullptr, 16)) {
        // add prefix if hex
        color = "#" + color;
    }
    region_style.set_color(color);

    // Set line width
    auto linewidth_str = GetProperty("linewidth", properties, true);
    int linewidth(casa::AnnotationBase::DEFAULT_LINEWIDTH), converted_linewidth;
    if (!linewidth_str.empty() && StringToInt(linewidth_str, converted_linewidth)) {
        linewidth = converted_linewidth;
    }
    region_style.set_line_width(linewidth);

    // Set line style
    auto linestyle = GetProperty("linestyle", properties, true);
    if (linestyle.empty()) {
        linestyle = "-"; // solid
    }
    if (linestyle == "-") { // solid line
        region_style.add_dash_list(0);
    } else if (linestyle == ":") { // dotted line
        region_style.add_dash_list(1);
    } else {
        region_style.add_dash_list(REGION_DASH_LENGTH); // CARTA default
    }

    // Set font
    ImportFontStyle(properties, region_style.mutable_annotation_style());

    return region_style;
}

void CrtfImporter::ImportFontStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    auto font = GetProperty("font", properties);
    if (!font.empty()) {
        annotation_style->set_font(font);
    }

    auto fontsize_str = GetProperty("fontsize", properties);
    if (!fontsize_str.empty()) {
        int fontsize;
        if (StringToInt(fontsize_str, fontsize)) {
            annotation_style->set_font_size(fontsize);
        }
    }

    auto font_style = GetProperty("fontstyle", properties);
    if (!font_style.empty()) {
        if (font_style == "bold-italic") {
            font_style = "bold_italic";
        }
        annotation_style->set_font_style(font_style);
    }
}

void CrtfImporter::ImportPointStyle(
    const std::string& symbol_char, std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    CARTA::PointAnnotationShape point_shape(CARTA::PointAnnotationShape::SQUARE);
    bool symthick(true);
    auto symthick_str = GetProperty("symthick", properties);
    if (!symthick_str.empty()) {
        symthick = (symthick_str == "1" ? true : false);
    }

    int symsize(1), converted_symsize;
    auto symsize_str = GetProperty("symsize", properties);
    if (!symsize_str.empty() && StringToInt(symsize_str, converted_symsize)) {
        symsize = converted_symsize;
    }

    if (symbol_char == "o") {
        point_shape = (symthick ? CARTA::PointAnnotationShape::CIRCLE : CARTA::PointAnnotationShape::CIRCLE_LINED);
    } else if (symbol_char == "s") {
        point_shape = (symthick ? CARTA::PointAnnotationShape::SQUARE : CARTA::PointAnnotationShape::BOX);
    } else if (symbol_char == "d" || symbol_char == "D") {
        point_shape = (symthick ? CARTA::PointAnnotationShape::DIAMOND : CARTA::PointAnnotationShape::DIAMOND_LINED);
    } else if (symbol_char == "+") {
        point_shape = CARTA::PointAnnotationShape::CROSS;
    } else if (symbol_char == "x") {
        point_shape = CARTA::PointAnnotationShape::X;
    }

    annotation_style->set_point_shape(point_shape);
    annotation_style->set_point_width(symsize);
}

bool CrtfImporter::GetBoxControlPoints(
    std::vector<std::string>& parameters, std::string& coord_frame, std::vector<CARTA::Point>& control_points, float& rotation) {
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region(parameters[param_index++]);
    casacore::Quantity p1, p2, p3, p4;

    // - import rotbox (always a polygon)
    // - import rectangle that forms a polygon not [blc, trc] rectangle in wcs
    // - import rectangle to linear coord sys (must be pixel)
    // - import when CRTF file contains "polyline" not supported by casa
    try {
        // Convert parameters to Quantity:
        casacore::readQuantity(p1, parameters[param_index++]);
        casacore::readQuantity(p2, parameters[param_index++]);
        casacore::readQuantity(p3, parameters[param_index++]);
        casacore::readQuantity(p4, parameters[param_index++]);

        if ((region == "rotbox") || (region == "textbox" && parameters.size() > 5)) {
            casacore::Quantity angle;
            casacore::readQuantity(angle, parameters[param_index++]);
            rotation = angle.get("deg").getValue();
        } else {
            rotation = 0.0;
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("{} import Quantity error: {}", region, err.getMesg());
        return false;
    }

    if (region == "rotbox" || region == "centerbox" || region == "textbox") {
        // cx, cy, width, height
        return GetCenterBoxPoints(region, p1, p2, p3, p4, coord_frame, control_points);
    } else {
        // blc_x, blc_y, trc_x, trc_y
        return GetRectBoxPoints(p1, p2, p3, p4, coord_frame, control_points);
    }

    return false;
}

bool CrtfImporter::GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
    casacore::Quantity& height, std::string& coord_frame, std::vector<CARTA::Point>& control_points) {
    try {
        // Convert center point cx, cy to pixel
        std::vector<casacore::Quantity> centerpoint;
        centerpoint.push_back(cx);
        centerpoint.push_back(cy);
        casacore::Vector<casacore::Double> pixel_coords;

        if (ConvertPointToPixels(_coord_sys, coord_frame, centerpoint, pixel_coords)) {
            // Set center control points
            control_points.push_back(Message::Point(pixel_coords));
            // Set width and height control points
            control_points.push_back(Message::Point(WorldToPixelLength(width, 0), WorldToPixelLength(height, 1)));
            return true;
        } else {
            spdlog::error("{} import conversion to pixels failed", region);
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("{} import error: {}", region, err.getMesg());
    }

    return false;
}

bool CrtfImporter::GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
    std::string& coord_frame, std::vector<CARTA::Point>& control_points) {
    bool converted(false);
    try {
        // Quantity math will fail if non-compatible units
        casacore::Quantity cx = (blcx + trcx) / 2.0;
        casacore::Quantity cy = (blcy + trcy) / 2.0;
        casacore::Quantity width = (trcx - blcx);
        casacore::Quantity height = (trcy - blcy);
        converted = GetCenterBoxPoints("box", cx, cy, width, height, coord_frame, control_points);
    } catch (const casacore::AipsError& err) {
        spdlog::error("box import Quantity error: {}", err.getMesg());
    }

    return converted;
}
