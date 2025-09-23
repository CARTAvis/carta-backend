/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Importer.cc: import regions in DS9 format

#include "Ds9Importer.h"

#include <casacore/casa/Quanta/QMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#define REGION_COLOR "#2EE6D6"
#define REGION_LINE_WIDTH 2

using namespace carta;

Ds9Importer::Ds9Importer(std::shared_ptr<casacore::CoordinateSystem> coord_sys, int file_id, const std::string& file, bool file_is_filename)
    : RegionImporter(coord_sys, file_id), _file_coord_frame("image"), _import_pixels(true) {
    _region_names = GetRegionTypeNames(CARTA::FileType::DS9_REG);
    _image_coord_frame = GetImageDirectionFrame(coord_sys);
    SetParserDelim(" ,()#");
    std::vector<std::string> file_lines = ReadRegionFile(file, file_is_filename, ';');
    SetFileLineRegions(file_lines);
}

void Ds9Importer::SetFileLineRegions(std::vector<std::string>& file_lines) {
    if (file_lines.empty()) {
        return;
    }

    // Map to check for DS9 keywords and convert to CASA
    InitDs9CoordMap();

    bool ds9_coord_ok(true);            // flag for invalid coord line
    bool is_combo_region(false);        // flag for combining two region lines (textbox + text)
    RegionProperties region_properties; // for combined regions

    for (auto& line : file_lines) {
        // skip blank line and comment (check for annotation region)
        if (line.empty() || IsCommentLine(line)) {
            continue;
        }

        // skip regions excluded for later analysis
        if (line[0] == '-') {
            continue;
        }

        if (line.find("global") != std::string::npos) {
            SetGlobals(line);
            continue;
        }

        // process coordinate system; global or for a region definition
        if (IsDs9Coord(line)) {
            // Get ready for conversion
            ds9_coord_ok = SetFileCoordFrame(line);
            if (!ds9_coord_ok) {
                std::string csys_error = "Coordinate system " + line + " in region file not supported.\n";
                _errors.append(csys_error);
            }
            continue;
        }

        // Skip lines defined in that coord sys if not ok
        if (ds9_coord_ok) {
            if (is_combo_region) {
                // region_properties is for textbox, now add style from text region
                RegionProperties text_properties = SetRegion(line);
                AddTextStyleToProperties(text_properties.style, region_properties);
            } else {
                region_properties = SetRegion(line);
            }
            is_combo_region = (line.find("textbox") == 0); // parsing line strips leading #

            if (!is_combo_region && region_properties.state.RegionDefined()) {
                _regions.push_back(region_properties);
            }
        }
    }
}

// Coordinate system handlers

void Ds9Importer::InitDs9CoordMap() {
    _coord_map[""] = "";
    _coord_map["physical"] = "";
    _coord_map["image"] = "";
    _coord_map["b1950"] = "B1950";
    _coord_map["fk4"] = "B1950";
    _coord_map["j2000"] = "J2000";
    _coord_map["fk5"] = "J2000";
    _coord_map["galactic"] = "GALACTIC";
    _coord_map["ecliptic"] = "ECLIPTIC";
    _coord_map["icrs"] = "ICRS";
    _coord_map["wcs"] = "UNSUPPORTED";
    _coord_map["wcsa"] = "UNSUPPORTED";
    _coord_map["linear"] = "UNSUPPORTED";
}

bool Ds9Importer::IsDs9Coord(std::string& file_line) {
    std::string line_lower(file_line);
    std::transform(file_line.begin(), file_line.end(), line_lower.begin(), ::tolower); // convert to lowercase
    return (_coord_map.find(line_lower) != _coord_map.end());
}

bool Ds9Importer::SetFileCoordFrame(std::string& ds9_coord) {
    std::transform(ds9_coord.begin(), ds9_coord.end(), ds9_coord.begin(), ::tolower);
    if (_coord_map.find(ds9_coord) != _coord_map.end()) {
        _file_coord_frame = _coord_map[ds9_coord];
    } else {
        _file_coord_frame = "UNSUPPORTED";
        _import_pixels = false;
        return false;
    }

    _import_pixels = ds9_coord.empty() || (ds9_coord == "physical") || (ds9_coord == "image");
    return true;
}

void Ds9Importer::SetGlobals(std::string& file_line) {
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(file_line, parameters, properties);
    // overwrite defaults
    _global_properties = properties;
}

RegionProperties Ds9Importer::SetRegion(std::string& file_line) {
    bool is_annotation(file_line[0] == '#');

    // Parse region definition into parameters and properties
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(file_line, parameters, properties);

    RegionProperties region_properties;
    if (parameters.empty()) {
        return region_properties;
    }

    // Region name
    std::string region_name(parameters[0]);
    if (parameters[1] == "point") { // e.g. "circle point" etc.
        region_name = "point";
    }
    if ((region_name[0] == '+') || (region_name[0] == '-')) {
        region_name = region_name.substr(1);
    }

    RegionState region_state;
    CARTA::RegionStyle region_style;

    if (region_name == "point" || region_name == "text") {
        region_state = ImportPointRegion(parameters, is_annotation);
        if (region_name == "text" && properties.find("textangle") != properties.end()) {
            region_state.rotation = std::stod(properties["textangle"]);
        }
    } else if (region_name == "ellipse") {
        region_state = ImportEllipseRegion(parameters, is_annotation);
    } else if (region_name == "circle") {
        region_state = ImportCircleRegion(parameters, is_annotation);
    } else if (region_name == "box" || region_name == "textbox") {
        region_state = ImportRectangleRegion(parameters, is_annotation);
    } else if (region_name == "line" || region_name == "polyline" || region_name == "polygon" || region_name == "segment") {
        region_state = ImportPolygonLineRegion(parameters, is_annotation);
    } else if (region_name == "vector") {
        region_state = ImportVectorRegion(parameters);
    } else if (region_name == "ruler") {
        region_state = ImportRulerRegion(parameters, properties, region_style);
    } else if (region_name == "compass") {
        region_state = ImportCompassRegion(parameters, properties, region_style);
    }

    if (region_state.RegionDefined()) {
        if (region_name != "ruler" && region_name != "compass") {
            region_style = ImportStyle(region_state.type, properties);
        }

        if (region_name == "textbox") {
            std::string align = GetProperty("align", properties);
            region_style.mutable_annotation_style()->set_text_position(GetTextPosition(align));
        }

        region_properties = RegionProperties(region_state, region_style);
    } else {
        std::vector<std::string> unsupported_regions{"projection", "annulus", "panda", "epanda", "bpanda", "composite"};
        if (std::find(unsupported_regions.begin(), unsupported_regions.end(), region_name) != unsupported_regions.end()) {
            _errors.append("DS9 " + region_name + " region not supported.\n");
        } else {
            bool is_annulus = (region_name == "ellipse" || region_name == "box") && parameters.size() > 6;
            if (!is_annulus) { // error already appended
                _errors.append("Invalid DS9 region syntax: " + region_name);
            }
        }
    }

    return region_properties;
}

RegionState Ds9Importer::ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation) {
    RegionState region_state;

    std::string region_name(parameters[0]);
    size_t first_param(1);
    if (parameters.size() > 1 && parameters[1] == "point") {
        region_name = "point";
        first_param = 2;
    }

    // point x y
    // <shape> point x y
    // text x y
    size_t nparam(parameters.size());
    if ((nparam < 3) || (nparam < 4 && first_param == 2)) {
        std::string syntax_error = region_name + " syntax error.\n";
        _errors.append(syntax_error);
        return region_state;
    }

    // Convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;
    for (size_t i = first_param; i < nparam; ++i) {
        std::string param(parameters[i]);
        bool is_angle(i == first_param + 1);
        bool is_xy(true);
        casacore::Quantity param_quantity;

        if (ParameterToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
            param_quantities.push_back(param_quantity);
        } else {
            return region_state;
        }
    }

    // Control points in pixel coordinates
    std::vector<CARTA::Point> control_points;
    if (_import_pixels) {
        control_points.push_back(Message::Point(param_quantities));
    } else if (_coord_sys) {
        casacore::Vector<casacore::Double> pixel_coords;
        if (ConvertPointToPixels(_coord_sys, _file_coord_frame, param_quantities, pixel_coords)) {
            control_points.push_back(Message::Point(pixel_coords));
        } else {
            std::string invalid_param("Failed to apply " + region_name + " to image.\n");
            _errors.append(invalid_param);
            return region_state;
        }
    }

    // Set RegionState
    CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNPOINT : CARTA::RegionType::POINT);
    if (region_name == "text") {
        type = CARTA::RegionType::ANNTEXT;
        // Set width/height of textbox to 0 for dynamic sizing in frontend
        control_points.push_back(Message::Point(0.0, 0.0));
    }
    float rotation(0.0);
    return RegionState(_file_id, type, control_points, rotation);
}

RegionState Ds9Importer::ImportCircleRegion(std::vector<std::string>& parameters, bool is_annotation) {
    RegionState region_state;
    auto region_name = parameters[0];

    if (parameters.size() == 4) {
        // For circle, create ellipse region (CARTA only has ellipse region) with no angle
        if (region_name == "circle") {
            region_name = "ellipse";
        }
        std::vector<std::string> ellipse_params = {region_name, parameters[1], parameters[2], parameters[3], parameters[3]};
        region_state = ImportEllipseRegion(ellipse_params, is_annotation);
    } else {
        std::string syntax_error = region_name + " syntax error.\n";
        _errors.append(syntax_error);
    }
    return region_state;
}

RegionState Ds9Importer::ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation) {
    RegionState region_state;
    auto region_name = parameters[0];
    size_t nparam(parameters.size());

    // ellipse x y radius radius [angle]
    // circle x y radius, compass x1 y1 length
    // For circle or compass radius1=radius2
    if ((nparam == 5) || (nparam == 6)) { // nparam 6 if angle
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            bool is_angle(i == 2);
            bool is_xy = (i == 1 || i == 2);
            casacore::Quantity param_quantity;

            if (ParameterToQuantity(parameters[i], is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // Control points in pixel coordinates
        std::vector<CARTA::Point> control_points;
        if (_import_pixels) {
            control_points.push_back(Message::Point(param_quantities, 0, 1));
            control_points.push_back(Message::Point(param_quantities, 2, 3));
        } else if (_coord_sys) {
            // cx, cy
            std::vector<casacore::Quantity> center_coords;
            center_coords.push_back(param_quantities[0]);
            center_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_coord_frame, center_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _errors.append("Failed to apply " + region_name + " to image.\n");
                return region_state;
            }

            // (bmaj, bmin) for ellipse, (length, length) for compass
            control_points.push_back(
                Message::Point(WorldToPixelLength(param_quantities[2], 0), WorldToPixelLength(param_quantities[3], 1)));
        }

        // Set RegionState
        CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNELLIPSE : CARTA::RegionType::ELLIPSE);
        if (region_name == "compass") {
            type = CARTA::RegionType::ANNCOMPASS;
        }

        // nparam includes region name, param_quantities does not!
        float rotation = (nparam > 5 ? param_quantities[4].getValue() : 0.0);
        if (control_points[1].x() != control_points[1].y()) {
            // bmaj != bmin: Adjust rotation for ellipse
            rotation -= 90.0;
            if (rotation < 0.0) {
                rotation += 360.0;
            }
        }
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else if (region_name == "ellipse" && nparam > 6) {
        // unsupported ellipse annulus: ellipse x y r11 r12 r21 r22 [angle]
        _errors.append("DS9 ellipse annulus region not supported.\n");
    } else {
        _errors.append(region_name + " syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation) {
    RegionState region_state;
    auto region_name = parameters[0];
    size_t nparam(parameters.size());

    // box x y width height [angle]
    if ((nparam == 5) || (nparam == 6)) {
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            std::string param(parameters[i]);
            bool is_angle(i == 5);
            bool is_xy(i == 1 || i == 2);
            casacore::Quantity param_quantity;
            if (ParameterToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // Control points in pixel coordinates
        std::vector<CARTA::Point> control_points;
        if (_import_pixels) {
            control_points.push_back(Message::Point(param_quantities, 0, 1));
            control_points.push_back(Message::Point(param_quantities, 2, 3));
        } else if (_coord_sys) {
            // cx, cy
            std::vector<casacore::Quantity> center_coords;
            center_coords.push_back(param_quantities[0]);
            center_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_coord_frame, center_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _errors.append("Failed to apply box to image.\n");
                return region_state;
            }

            // width, height
            control_points.push_back(
                Message::Point(WorldToPixelLength(param_quantities[2], 0), WorldToPixelLength(param_quantities[3], 1)));
        }

        // Create RegionState
        CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNRECTANGLE : CARTA::RegionType::RECTANGLE);
        if (region_name == "textbox") {
            type = CARTA::RegionType::ANNTEXT;
        }
        float rotation(0.0);
        if (nparam > 5) {
            rotation = param_quantities[4].getValue();
        }
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else if (nparam > 6) {
        // unsupported box annulus: box x y w1 h1 w2 h2 [angle]
        _errors.append("DS9 box annulus region not supported.\n");
    } else {
        _errors.append("box syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation) {
    RegionState region_state;
    std::string region_name(parameters[0]);
    size_t nparam(parameters.size());

    // For regions defined with list of points: polygon, line, polyline, ruler
    // All regions defined by at least two points x1 y1 x2 y2 [x3 y3 ...]
    if ((nparam % 2) != 1) { // parameters[0] is region name
        _errors.append(region_name + " syntax error.\n");
        return region_state;
    }

    // Convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;

    for (size_t i = 1; i < nparam; ++i) {
        std::string param(parameters[i]);
        bool is_angle((i % 2) == 0); // second point in each pair in angle format
        bool is_xy(true);            // all parameters are xy point coordinates
        casacore::Quantity param_quantity;

        if (ParameterToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
            param_quantities.push_back(param_quantity);
        } else {
            return region_state;
        }
    }

    // Control points in pixel coordinates
    std::vector<CARTA::Point> control_points;
    for (size_t i = 0; i < param_quantities.size(); i += 2) {
        if (_import_pixels) {
            control_points.push_back(Message::Point(param_quantities, i, i + 1));
        } else if (_coord_sys) {
            std::vector<casacore::Quantity> point;
            point.push_back(param_quantities[i]);
            point.push_back(param_quantities[i + 1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_coord_frame, point, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _errors.append("Failed to apply " + region_name + " to image.\n");
                return region_state;
            }
        }
    }

    // Set RegionState
    CARTA::RegionType type;
    if (region_name == "line") {
        type = (is_annotation ? CARTA::RegionType::ANNLINE : CARTA::RegionType::LINE);
    } else if (region_name == "polyline") {
        type = (is_annotation ? CARTA::RegionType::ANNPOLYLINE : CARTA::RegionType::POLYLINE);
    } else if (region_name == "segment") {
        type = CARTA::RegionType::ANNPOLYLINE;
    } else if (region_name == "polygon") {
        type = (is_annotation ? CARTA::RegionType::ANNPOLYGON : CARTA::RegionType::POLYGON);
    } else if (region_name == "ruler") {
        type = CARTA::RegionType::ANNRULER;
    } else {
        return region_state;
    }

    float rotation(0.0);
    region_state = RegionState(_file_id, type, control_points, rotation);
    return region_state;
}

RegionState Ds9Importer::ImportVectorRegion(std::vector<std::string>& parameters) {
    RegionState region_state;
    std::string region_name(parameters[0]);
    size_t nparam(parameters.size());

    // vector x1 y1 length angle
    if (nparam == 5) {
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            std::string param(parameters[i]);
            bool is_angle(i == 2); // dec/long
            bool is_xy = (i == 1 || i == 2);
            casacore::Quantity param_quantity;

            if (ParameterToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // With no unit, ParameterToQuantity sets to "pixel" if _import_pixels, but angle is in degrees
        auto angle = param_quantities[3];
        if (angle.getUnit() == "pixel") {
            angle.setUnit("deg");
        }
        auto angle_rad = angle.get("rad").getValue();

        auto length = param_quantities[2];
        auto xlength = cos(angle_rad);
        auto ylength = sin(angle_rad);

        // Control points in pixel coordinates are endpoints x1 y1 x2 y2
        std::vector<CARTA::Point> control_points;
        if (_import_pixels) {
            auto x1 = param_quantities[0].getValue();
            auto y1 = param_quantities[1].getValue();

            // (length, angle) to (x2, y2)
            auto x2 = x1 + (length.getValue() * xlength);
            auto y2 = y1 + (length.getValue() * ylength);
            control_points.push_back(Message::Point(x1, y1));
            control_points.push_back(Message::Point(x2, y2));
        } else if (_coord_sys) {
            // x1, y1
            std::vector<casacore::Quantity> point_coords;
            point_coords.push_back(param_quantities[0]);
            point_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_coord_frame, point_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _errors.append("Failed to apply " + region_name + " to image.\n");
                return region_state;
            }

            // (length, angle) to (x2, y2)
            auto dx = WorldToPixelLength(length * xlength, 0) * (xlength < 0 ? -1.0 : 1.0);
            auto dy = WorldToPixelLength(length * ylength, 1) * (ylength < 0 ? -1.0 : 1.0);
            control_points.push_back(Message::Point(pixel_coords[0] + dx, pixel_coords[1] + dy));
        }

        // Set RegionState
        CARTA::RegionType type(CARTA::RegionType::ANNVECTOR);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _errors.append(region_name + " syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportRulerRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    region_style = ImportStyle(CARTA::RegionType::ANNRULER, properties);

    // Get ruler coordinate
    std::string ruler_coord;
    auto ruler_properties = GetProperty("ruler", properties);
    if (!ruler_properties.empty()) {
        ImportRulerStyle(ruler_properties, ruler_coord);
    }

    RegionState region_state;
    if (!ruler_coord.empty() && (ruler_coord != _file_coord_frame)) {
        // Use ruler coordinate to set RegionState
        std::string file_coord_frame = _file_coord_frame; // copy
        if (SetFileCoordFrame(ruler_coord)) {
            region_state = ImportPolygonLineRegion(parameters);
            SetFileCoordFrame(file_coord_frame); // restore
        }
    } else {
        // Use file coordinate frame to set RegionState
        region_state = ImportPolygonLineRegion(parameters);
    }

    return region_state;
}

RegionState Ds9Importer::ImportCompassRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    region_style = ImportStyle(CARTA::RegionType::ANNCOMPASS, properties);

    // Get compass coordinate
    std::string compass_coord;
    auto compass_properties = GetProperty("compass", properties);
    if (!compass_properties.empty()) {
        ImportCompassStyle(compass_properties, compass_coord, region_style.mutable_annotation_style());
    }

    RegionState region_state;
    if (!compass_coord.empty() && (compass_coord != _file_coord_frame)) {
        // Use compass coordinate to set RegionState
        std::string file_coord_frame = _file_coord_frame; // copy
        if (SetFileCoordFrame(compass_coord)) {
            region_state = ImportCircleRegion(parameters);
            SetFileCoordFrame(file_coord_frame); // restore
        }
    } else {
        // Use file coordinate to set RegionState
        region_state = ImportCircleRegion(parameters);
    }

    return region_state;
}

CARTA::RegionStyle Ds9Importer::ImportStyle(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties) {
    CARTA::RegionStyle region_style;
    CARTA::AnnotationStyle* annotation_style(nullptr);
    bool is_annotation = (region_type > CARTA::RegionType::POLYGON);
    if (is_annotation) {
        annotation_style = region_style.mutable_annotation_style();
    }

    // name for most regions, or text for text region
    auto text_name = GetProperty("text", properties);
    if (!text_name.empty()) {
        if (text_name.front() == '{' && text_name.back() == '}') {
            text_name = text_name.substr(1, text_name.length() - 2);
        }

        // "text" property used for text not name
        if (region_type == CARTA::RegionType::ANNTEXT) {
            annotation_style->set_text_label0(text_name);
        } else {
            region_style.set_name(text_name);
        }
    }

    // color
    auto color = GetProperty("color", properties, true);
    if (color.empty()) {
        region_style.set_color(REGION_COLOR);
    } else {
        region_style.set_color(FormatColor(color));
    }

    // line width
    auto width_str = GetProperty("width", properties, true);
    int width(REGION_LINE_WIDTH), convert_width;
    if (!width_str.empty() && StringToInt(width_str, convert_width)) {
        width = convert_width;
    }
    region_style.set_line_width(width);

    // line dash
    auto dash = GetProperty("dash", properties, true);
    auto dashlist = GetProperty("dashlist", properties, true);
    if (dash == "1" && !dashlist.empty()) {
        // Convert string to list of int values
        std::vector<std::string> dash_list_str;
        SplitString(dashlist, ' ', dash_list_str);
        std::vector<int> dash_list;

        for (size_t i = 0; i < dash_list_str.size(); ++i) {
            int val;
            if (StringToInt(dash_list_str[i], val)) {
                dash_list.push_back(val);
            }
        }
        *region_style.mutable_dash_list() = {dash_list.begin(), dash_list.end()};
    }

    // Set annotation style parameters for annotation regions
    if (region_type == CARTA::RegionType::ANNPOINT) {
        ImportPointStyle(properties, annotation_style);
    }
    if (region_type > CARTA::RegionType::POLYGON) {
        ImportFontStyle(properties, annotation_style);
    }

    return region_style;
}

void Ds9Importer::ImportPointStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    auto point = GetProperty("point", properties);
    if (point.empty()) {
        return;
    }

    // point=[circle|box|diamond|cross|x|arrow|boxcircle] [size]
    std::vector<std::string> params;
    SplitString(point, ' ', params);
    int param_index(0), point_width;

    while (param_index < params.size()) {
        auto param = params[param_index++];

        if (StringToInt(param, point_width)) {
            annotation_style->set_point_width(point_width);
        } else {
            // Set point shape
            auto fill = GetProperty("fill", properties);
            auto point_shape = CARTA::PointAnnotationShape::SQUARE;
            if (param == "circle") {
                point_shape = (fill == "1" ? CARTA::PointAnnotationShape::CIRCLE : CARTA::PointAnnotationShape::CIRCLE_LINED);
            } else if (param == "box") {
                point_shape = (fill == "1" ? CARTA::PointAnnotationShape::SQUARE : CARTA::PointAnnotationShape::BOX);
            } else if (param == "diamond") {
                point_shape = (fill == "1" ? CARTA::PointAnnotationShape::DIAMOND : CARTA::PointAnnotationShape::DIAMOND_LINED);
            } else if (param == "cross") {
                point_shape = CARTA::PointAnnotationShape::CROSS;
            } else if (param == "x") {
                point_shape = CARTA::PointAnnotationShape::X;
            }
            annotation_style->set_point_shape(point_shape);
        }
    }
}

void Ds9Importer::ImportFontStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    std::string font = GetProperty("font", properties, true);
    if (font.empty()) {
        return;
    }

    // font family, size, weight, and slant of any text to be displayed along with the region
    // e.g. font="times 12 bold italic"
    std::vector<std::string> font_parameters;
    SplitString(font, ' ', font_parameters);

    if (font_parameters.size() == 4) {
        // font
        auto font = font_parameters[0];
        font[0] = std::toupper(font[0]);
        annotation_style->set_font(font);
        // size
        int fontsize(0);
        if (StringToInt(font_parameters[1], fontsize)) {
            annotation_style->set_font_size(fontsize);
        }
        // weight
        bool is_bold = (font_parameters[2] == "bold");
        // slant
        bool is_italic = (font_parameters[3] == "italic");

        if (is_bold && is_italic) {
            annotation_style->set_font_style("bold_italic");
        } else if (is_bold) {
            annotation_style->set_font_style("bold");
        } else if (is_italic) {
            annotation_style->set_font_style("italic");
        } else {
            annotation_style->set_font_style("normal");
        }
    }
}

bool Ds9Importer::ParameterToQuantity(
    std::string& parameter, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity) {
    if (Ds9ToCasacoreUnit(region_name, parameter)) {
        if (is_angle) {
            ConvertTimeFormatToAngle(parameter);
        }

        if (readQuantity(param_quantity, parameter)) {
            if (param_quantity.getUnit().empty()) {
                if (_import_pixels) {
                    // Change from 1-based to 0-based image coordinate for all points in (x, y)
                    if (is_xy) {
                        param_quantity.setValue(param_quantity.getValue() - 1);
                    }
                    param_quantity.setUnit("pixel");
                } else {
                    param_quantity.setUnit("deg");
                }
            }
            return true;
        } else {
            std::string invalid_param("Invalid " + region_name + " parameter " + parameter + ".\n");
            _errors.append(invalid_param);
            return false;
        }
    }
    return false;
}

bool Ds9Importer::Ds9ToCasacoreUnit(const std::string& region_name, std::string& parameter) {
    bool valid(false);
    std::string error_prefix(region_name + " invalid parameter ");

    // use stod to find index of unit in string (after numeric value)
    size_t idx;
    try {
        double val = stod(parameter, &idx); // string to double
    } catch (std::invalid_argument& err) {
        std::string invalid_arg(error_prefix + parameter + ", not a numeric value.\n");
        _errors.append(invalid_arg);
        return valid;
    }

    size_t param_length(parameter.length());
    valid = (param_length == idx); // no unit is valid
    if (!valid) {                  // check unit/format
        if (param_length == (idx + 1)) {
            // DS9 units are a single character
            const char unit = parameter.back();
            std::string casacore_unit;
            if (unit == 'd') {
                casacore_unit = "deg";
                valid = true;
            } else if (unit == 'r') {
                casacore_unit = "rad";
                valid = true;
            } else if (unit == 'p') {
                casacore_unit = "pixel";
                valid = true;
            } else if (unit == 'i') {
                casacore_unit = "pixel";
                valid = true;
            } else if ((unit == '"') || (unit == '\'')) {
                // casacore unit for min, sec is the same
                valid = true;
            } else {
                std::string invalid_unit(error_prefix + "unit: " + parameter + ".\n");
                _errors.append(invalid_unit);
                valid = false;
            }

            if (!casacore_unit.empty()) {
                // replace DS9 unit with casacore unit
                parameter.pop_back();
                parameter.append(casacore_unit);
            }
        } else {
            // check for hms, dms formats
            const char* param_carray = parameter.c_str();
            float h, m, s;
            valid = ((sscanf(param_carray, "%f:%f:%f", &h, &m, &s) == 3) || (sscanf(param_carray, "%fh%fm%fs", &h, &m, &s) == 3) ||
                     (sscanf(param_carray, "%fd%fm%fs", &h, &m, &s) == 3));
            if (!valid) {
                // Unit not a single character or time/angle format
                std::string invalid_unit(error_prefix + "unit: " + parameter + ".\n");
                _errors.append(invalid_unit);
            }
        }
    }
    return valid;
}

void Ds9Importer::ConvertTimeFormatToAngle(std::string& parameter) {
    // Convert sexagesimal format dd:mm::ss.ssss to angle format dd.mm.ss.ssss, for casacore readQuantity
    for (std::string::iterator it = parameter.begin(); it != parameter.end(); ++it) {
        if (*it == ':') {
            *it = '.';
        }
    }
}
