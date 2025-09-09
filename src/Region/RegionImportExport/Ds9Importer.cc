/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Importer.cc: import regions in DS9 format

#include "Ds9Importer.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#define REGION_COLOR "#2EE6D6"
#define REGION_LINE_WIDTH 2

using namespace carta;

Ds9Importer::Ds9Importer(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename)
    : RegionImporter(image_coord_sys, file_id), _file_ref_frame("image"), _pixel_coord(true) {
    // Import regions in DS9 format
    SetParserDelim(" ,()#");
    _region_names = GetRegionTypeNames(CARTA::FileType::DS9_REG);
    std::vector<std::string> lines = ReadRegionFile(file, file_is_filename, ';');
    ProcessFileLines(lines);
}

void Ds9Importer::ProcessFileLines(std::vector<std::string>& lines) {
    // Process or ignore each file line
    if (lines.empty()) {
        return;
    }

    // Map to check for DS9 keywords and convert to CASA
    InitDs9CoordMap();

    bool ds9_coord_sys_ok(true);        // flag for invalid coord sys lines
    bool is_combo_region(false);        // flag for combining two region lines (textbox + text)
    RegionProperties region_properties; // for combined regions

    for (auto& line : lines) {
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
        if (IsDs9CoordSysKeyword(line)) {
            // Get ready for conversion
            ds9_coord_sys_ok = SetFileReferenceFrame(line);

            if (!ds9_coord_sys_ok) {
                std::string csys_error = "coord sys " + line + " not supported.\n";
                _import_errors.append(csys_error);
            }
            continue;
        }

        // Skip lines defined in that coord sys if not ok
        if (ds9_coord_sys_ok) {
            if (is_combo_region) {
                // region_properties is for textbox, now add style from text region
                RegionProperties text_properties = SetRegion(line);
                AddTextStyleToProperties(text_properties.style, region_properties);
            } else {
                region_properties = SetRegion(line);
            }
            is_combo_region = (line.find("textbox") == 0); // parsing line strips leading #

            if (!is_combo_region && region_properties.state.RegionDefined()) {
                _import_regions.push_back(region_properties);
            }
        }
    }
}

// Coordinate system handlers

void Ds9Importer::InitDs9CoordMap() {
    // for converting coordinate system from DS9 to casacore
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

bool Ds9Importer::IsDs9CoordSysKeyword(std::string& input_line) {
    // Check if region file line is coordinate in map
    std::string input_lower(input_line);
    std::transform(input_line.begin(), input_line.end(), input_lower.begin(), ::tolower); // convert to lowercase
    return (_coord_map.find(input_lower) != _coord_map.end());
}

bool Ds9Importer::SetFileReferenceFrame(std::string& ds9_coord) {
    // Convert DS9 coord string in region file to CASA reference frame.
    // Returns whether conversion was successful or undefined/not supported.
    // Convert in-place to lowercase for map
    std::transform(ds9_coord.begin(), ds9_coord.end(), ds9_coord.begin(), ::tolower);

    // Convert to CASA and set pixel_coord
    if (_coord_map.find(ds9_coord) != _coord_map.end()) {
        _file_ref_frame = _coord_map[ds9_coord];
    } else {
        _file_ref_frame = "UNSUPPORTED";
        _pixel_coord = false;
        return false;
    }

    if ((ds9_coord != "physical") && (ds9_coord != "image")) {
        _pixel_coord = false;

        // Set image reference frame for conversion
        if (_image_ref_frame.empty()) {
            SetImageReferenceFrame();
        }
    }

    return true;
}

void Ds9Importer::SetImageReferenceFrame() {
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

void Ds9Importer::SetGlobals(std::string& global_line) {
    // Set global properties using file line parser
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(global_line, parameters, properties);
    _global_properties = properties;
}

RegionProperties Ds9Importer::SetRegion(std::string& region_definition) {
    // Convert ds9 region definition into RegionProperties (RegionState, RegionStyle)
    // Parse region definition into parameters and properties
    RegionProperties region_properties;
    bool is_annotation(region_definition[0] == '#');
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(region_definition, parameters, properties);

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
        // Also set RegionStyle from properties and use coordinate system for conversion if needed
        region_state = ImportRulerRegion(parameters, properties, region_style);
    } else if (region_name == "compass") {
        // Also set RegionStyle from properties and use coordinate system for conversion if needed
        region_state = ImportCompassRegion(parameters, properties, region_style);
    }

    if (region_state.RegionDefined()) {
        if (region_name != "ruler" && region_name != "compass") {
            region_style = ImportStyleParameters(region_state.type, properties);
        }

        if (region_name == "textbox") {
            std::string align = GetProperty("align", properties);
            region_style.mutable_annotation_style()->set_text_position(GetTextPosition(align));
        }

        region_properties = RegionProperties(region_state, region_style);
    } else {
        std::vector<std::string> unsupported_regions{"projection", "annulus", "panda", "epanda", "bpanda", "composite"};
        if (std::find(unsupported_regions.begin(), unsupported_regions.end(), region_name) != unsupported_regions.end()) {
            _import_errors.append("DS9 " + region_name + " region not supported.\n");
        } else {
            bool is_annulus = (region_name == "ellipse" || region_name == "box") && parameters.size() > 6;
            if (!is_annulus) { // error already appended
                _import_errors.append("Invalid DS9 region syntax: " + region_definition);
            }
        }
    }

    return region_properties;
}

RegionState Ds9Importer::ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation) {
    // Import DS9 point into RegionState
    // point x y, <shape> point x y, or text x y
    RegionState region_state;
    std::string region_name(parameters[0]);
    size_t first_param(1);
    if (parameters.size() > 1 && parameters[1] == "point") {
        region_name = "point";
        first_param = 2;
    }

    size_t nparam(parameters.size());
    if ((nparam < 3) || (nparam < 4 && first_param == 2)) {
        std::string syntax_error = region_name + " syntax error.\n";
        _import_errors.append(syntax_error);
        return region_state;
    }

    // Convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;

    for (size_t i = first_param; i < nparam; ++i) {
        std::string param(parameters[i]);
        bool is_angle(i == first_param + 1);
        bool is_xy(true);
        casacore::Quantity param_quantity;

        if (ParamToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
            param_quantities.push_back(param_quantity);
        } else {
            return region_state;
        }
    }

    // Control points in pixel coordinates
    std::vector<CARTA::Point> control_points;
    if (_pixel_coord) {
        control_points.push_back(Message::Point(param_quantities));
    } else if (_coord_sys) {
        casacore::Vector<casacore::Double> pixel_coords;
        if (ConvertPointToPixels(_coord_sys, _file_ref_frame, param_quantities, pixel_coords)) {
            control_points.push_back(Message::Point(pixel_coords));
        } else {
            std::string invalid_param("Failed to apply " + region_name + " to image.\n");
            _import_errors.append(invalid_param);
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
    // Import DS9 circle and compass into RegionState
    // circle x y radius or compass x1 y1 length
    // Convert params to ellipse region (CARTA only has ellipse region) with no angle
    RegionState region_state;
    auto region_name = parameters[0];

    if (parameters.size() == 4) {
        if (region_name == "circle") {
            region_name = "ellipse";
        }
        std::vector<std::string> ellipse_params = {region_name, parameters[1], parameters[2], parameters[3], parameters[3]};
        region_state = ImportEllipseRegion(ellipse_params, is_annotation);
    } else {
        std::string syntax_error = region_name + " syntax error.\n";
        _import_errors.append(syntax_error);
    }
    return region_state;
}

RegionState Ds9Importer::ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation) {
    // Import DS9 ellipse into RegionState
    // ellipse x y radius radius [angle], circle x y radius radius, compass x1 y1 length length
    RegionState region_state;
    auto region_name = parameters[0];
    size_t nparam(parameters.size());

    if ((nparam == 5) || (nparam == 6)) { // 6 if angle
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            std::string param(parameters[i]);
            bool is_angle(i == 2);
            bool is_xy = (i == 1 || i == 2);
            casacore::Quantity param_quantity;

            if (ParamToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // Control points in pixel coordinates
        std::vector<CARTA::Point> control_points;
        if (_pixel_coord) {
            control_points.push_back(Message::Point(param_quantities, 0, 1));
            control_points.push_back(Message::Point(param_quantities, 2, 3));
        } else if (_coord_sys) {
            // cx, cy
            std::vector<casacore::Quantity> center_coords;
            center_coords.push_back(param_quantities[0]);
            center_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_ref_frame, center_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _import_errors.append("Failed to apply " + region_name + " to image.\n");
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
            // Adjust rotation for ellipse
            rotation -= 90.0;
            if (rotation < 0.0) {
                rotation += 360.0;
            }
        }
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else if (region_name == "ellipse" && nparam > 6) {
        // unsupported ellipse annulus: ellipse x y r11 r12 r21 r22 [angle]
        _import_errors.append("DS9 ellipse annulus region not supported.\n");
    } else {
        _import_errors.append(region_name + " syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation) {
    // Import DS9 box into RegionState
    // box x y width height [angle]
    RegionState region_state;
    auto region_name = parameters[0];
    size_t nparam(parameters.size());

    if ((nparam == 5) || (nparam == 6)) {
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            std::string param(parameters[i]);
            bool is_angle(i == 2);
            bool is_xy(i == 1 || i == 2);
            casacore::Quantity param_quantity;

            if (ParamToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // Control points in pixel coordinates
        std::vector<CARTA::Point> control_points;
        if (_pixel_coord) {
            control_points.push_back(Message::Point(param_quantities, 0, 1));
            control_points.push_back(Message::Point(param_quantities, 2, 3));
        } else if (_coord_sys) {
            // cx, cy
            std::vector<casacore::Quantity> center_coords;
            center_coords.push_back(param_quantities[0]);
            center_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_ref_frame, center_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _import_errors.append("Failed to apply box to image.\n");
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
        _import_errors.append("DS9 box annulus region not supported.\n");
    } else {
        _import_errors.append("box syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation) {
    // Import DS9 polygon/line-type region into RegionState.
    // Regions defined by at least two points x1 y1 x2 y2 [x3 y3 ...]
    // polygon, line, polyline, and ruler
    RegionState region_state;
    std::string region_name(parameters[0]);
    size_t nparam(parameters.size());

    if ((nparam % 2) != 1) { // parameters[0] is region name
        _import_errors.append(region_name + " syntax error.\n");
        return region_state;
    }

    // Convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;

    for (size_t i = 1; i < nparam; ++i) {
        std::string param(parameters[i]);
        bool is_angle((i % 2) == 0); // second point in each pair in angle format
        bool is_xy(true);            // all parameters are xy point coordinates
        casacore::Quantity param_quantity;

        if (ParamToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
            param_quantities.push_back(param_quantity);
        } else {
            return region_state;
        }
    }

    // Control points in pixel coordinates
    std::vector<CARTA::Point> control_points;
    for (size_t i = 0; i < param_quantities.size(); i += 2) {
        if (_pixel_coord) {
            control_points.push_back(Message::Point(param_quantities, i, i + 1));
        } else if (_coord_sys) {
            std::vector<casacore::Quantity> point;
            point.push_back(param_quantities[i]);
            point.push_back(param_quantities[i + 1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_ref_frame, point, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _import_errors.append("Failed to apply " + region_name + " to image.\n");
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
    // Import DS9 vector into RegionState.
    // vector x1 y1 length angle
    RegionState region_state;
    std::string region_name(parameters[0]);
    size_t nparam(parameters.size());

    if (nparam == 5) {
        // Convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;

        for (size_t i = 1; i < nparam; ++i) {
            std::string param(parameters[i]);
            bool is_angle(i == 2);
            bool is_xy = (i == 1 || i == 2);
            casacore::Quantity param_quantity;

            if (ParamToQuantity(param, is_angle, is_xy, region_name, param_quantity)) {
                param_quantities.push_back(param_quantity);
            } else {
                return region_state;
            }
        }

        // With no unit, ParamToQuantity sets to "pixel" if _pixel_coord, but angle is in degrees
        auto angle = param_quantities[3];
        if (angle.getUnit() == "pixel") {
            angle.setUnit("deg");
        }
        auto angle_rad = angle.get("rad").getValue();

        // Control points in pixel coordinates
        std::vector<CARTA::Point> control_points;
        if (_pixel_coord) {
            auto x1 = param_quantities[0].getValue();
            auto y1 = param_quantities[1].getValue();
            auto length = param_quantities[2].getValue();
            auto x2 = x1 + (length * cos(angle_rad));
            auto y2 = y1 + (length * sin(angle_rad));
            control_points.push_back(Message::Point(x1, y1));
            control_points.push_back(Message::Point(x2, y2));
        } else if (_coord_sys) {
            // x1, y1
            std::vector<casacore::Quantity> point_coords;
            point_coords.push_back(param_quantities[0]);
            point_coords.push_back(param_quantities[1]);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(_coord_sys, _file_ref_frame, point_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _import_errors.append("Failed to apply " + region_name + " to image.\n");
                return region_state;
            }

            // (length, angle) to (x2, y2)
            auto length = param_quantities[2].getValue();
            auto xlength = cos(angle_rad);
            auto ylength = sin(angle_rad);
            auto dx = WorldToPixelLength(length * xlength, 0) * (xlength < 0 ? -1.0 : 1.0);
            auto dy = WorldToPixelLength(length * ylength, 1) * (ylength < 0 ? -1.0 : 1.0);
            control_points.push_back(Message::Point(pixel_coords[0] + dx, pixel_coords[1] + dy));
        }

        // Set RegionState
        CARTA::RegionType type(CARTA::RegionType::ANNVECTOR);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else if (nparam > 6) {
        // unsupported ellipse annulus: ellipse x y r11 r12 r21 r22 [angle]
        _import_errors.append("Unsupported " + region_name + " definition.\n");
    } else {
        _import_errors.append(region_name + " syntax error.\n");
    }
    return region_state;
}

RegionState Ds9Importer::ImportRulerRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    // Import ruler using parameters and properties.
    // Start with properties to get coordinate system
    region_style = ImportStyleParameters(CARTA::RegionType::ANNRULER, properties);
    std::string coordinate_system;
    auto ruler_properties = GetProperty("ruler", properties);
    if (!ruler_properties.empty()) {
        ImportRulerStyle(ruler_properties, coordinate_system);
    }

    // Use parameters and ruler coordinate system to set RegionState
    RegionState region_state;
    if (!coordinate_system.empty() && (coordinate_system != _file_ref_frame)) {
        std::string file_ref_frame = _file_ref_frame;
        if (SetFileReferenceFrame(coordinate_system)) {
            _pixel_coord = file_ref_frame.empty();
            region_state = ImportPolygonLineRegion(parameters);
            _file_ref_frame = file_ref_frame;
        }
    } else {
        region_state = ImportPolygonLineRegion(parameters);
    }

    return region_state;
}

RegionState Ds9Importer::ImportCompassRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    // Import compass using parameters and properties.
    // Start with properties to get coordinate system
    region_style = ImportStyleParameters(CARTA::RegionType::ANNCOMPASS, properties);
    std::string coordinate_system;
    auto compass_properties = GetProperty("compass", properties);
    if (!compass_properties.empty()) {
        ImportCompassStyle(compass_properties, coordinate_system, region_style.mutable_annotation_style());
    }

    // Use parameters and coordinate system to set RegionState
    RegionState region_state;
    if (!coordinate_system.empty()) {
        std::string file_ref_frame = _file_ref_frame;
        if (SetFileReferenceFrame(coordinate_system)) {
            _pixel_coord = file_ref_frame.empty();
            region_state = ImportCircleRegion(parameters);
            _file_ref_frame = file_ref_frame;
        }
    } else {
        region_state = ImportCircleRegion(parameters);
    }

    return region_state;
}

CARTA::RegionStyle Ds9Importer::ImportStyleParameters(
    CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties) {
    // Get style params from properties
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

    if (region_type == CARTA::RegionType::ANNPOINT) {
        ImportPointStyleParameters(properties, annotation_style); // point shape/size, only for ann region
    }
    if (region_type > CARTA::RegionType::POLYGON) {
        ImportFontStyleParameters(properties, annotation_style); // only for ann regions
    }

    return region_style;
}

void Ds9Importer::ImportPointStyleParameters(
    std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    // DS9 combines parameters in one string
    auto point = GetProperty("point", properties);
    if (point.empty()) {
        return;
    }

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

void Ds9Importer::ImportFontStyleParameters(
    std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    // DS9 combines parameters in one string
    std::string font = GetProperty("font", properties, true);
    if (!font.empty()) {
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
}

bool Ds9Importer::ParamToQuantity(
    std::string& param, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity) {
    // Convert param string to casacore Quantity
    if (Ds9ToCasacoreUnit(param, region_name)) {
        if (is_angle) {
            ConvertTimeFormatToAngle(param);
        }

        if (readQuantity(param_quantity, param)) {
            if (param_quantity.getUnit().empty()) {
                if (_pixel_coord) {
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
            std::string invalid_param("Invalid " + region_name + " parameter " + param + ".\n");
            _import_errors.append(invalid_param);
            return false;
        }
    }
    return false;
}

bool Ds9Importer::Ds9ToCasacoreUnit(std::string& param, const std::string& region_name) {
    // Replace DS9 unit with casacore::Quantity unit in parameter string for readQuantity
    // Returns whether valid ds9 parameter
    bool valid(false);
    std::string error_prefix(region_name + " invalid parameter ");

    // use stod to find index of unit in string (after numeric value)
    size_t idx;
    try {
        double val = stod(param, &idx); // string to double
    } catch (std::invalid_argument& err) {
        std::string invalid_arg(error_prefix + param + ", not a numeric value.\n");
        _import_errors.append(invalid_arg);
        return valid;
    }

    size_t param_length(param.length());
    valid = (param_length == idx); // no unit is valid
    if (!valid) {                  // check unit/format
        if (param_length == (idx + 1)) {
            // DS9 units are a single character
            const char unit = param.back();
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
                std::string invalid_unit(error_prefix + "unit: " + param + ".\n");
                _import_errors.append(invalid_unit);
                valid = false;
            }

            if (!casacore_unit.empty()) {
                // replace DS9 unit with casacore unit
                param.pop_back();
                param.append(casacore_unit);
            }
        } else {
            // check for hms, dms formats
            const char* param_carray = param.c_str();
            float h, m, s;
            valid = ((sscanf(param_carray, "%f:%f:%f", &h, &m, &s) == 3) || (sscanf(param_carray, "%fh%fm%fs", &h, &m, &s) == 3) ||
                     (sscanf(param_carray, "%fd%fm%fs", &h, &m, &s) == 3));
            if (!valid) {
                // Unit not a single character or time/angle format
                std::string invalid_unit(error_prefix + "unit: " + param + ".\n");
                _import_errors.append(invalid_unit);
            }
        }
    }
    return valid;
}

void Ds9Importer::ConvertTimeFormatToAngle(std::string& parameter) {
    // If parameter is in sexagesimal format dd:mm::ss.ssss, convert to angle format dd.mm.ss.ssss for readQuantity
    for (std::string::iterator it = parameter.begin(); it != parameter.end(); ++it) {
        if (*it == ':') {
            *it = '.';
        }
    }
}
