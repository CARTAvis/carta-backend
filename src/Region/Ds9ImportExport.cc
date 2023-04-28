/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9ImportExport.cc: import and export regions in DS9 format

#include "Ds9ImportExport.h"

#include <iomanip>

#include <spdlog/fmt/fmt.h>

#include <casacore/casa/Quanta/QMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#include "Util/App.h"
#include "Util/String.h"

using namespace carta;

Ds9ImportExport::Ds9ImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape,
    int file_id, const std::string& file, bool file_is_filename)
    : RegionImportExport(image_coord_sys, image_shape, file_id), _file_ref_frame("image"), _pixel_coord(true) {
    // Import regions in DS9 format
    SetParserDelim(" ,()#");
    AddRegionNames(); // map <type, string>
    std::vector<std::string> lines = ReadRegionFile(file, file_is_filename, ';');
    ProcessFileLines(lines);
}

Ds9ImportExport::Ds9ImportExport(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coord)
    : RegionImportExport(image_coord_sys, image_shape), _pixel_coord(pixel_coord) {
    // Export regions to DS9 format
    // Set properties for file header
    InitGlobalProperties();
    AddRegionNames(); // map <type, string>

    SetImageReferenceFrame(); // casacore frame, from coordinate system
    InitDs9CoordMap();        // to convert casacore to DS9 coordinate system for annotation regions
    for (auto& coord : _coord_map) {
        if (coord.second == _image_ref_frame) {
            _image_ref_frame = coord.first;
            break;
        }
    }
    // Multiple options for these image frames, use fk* version
    if (_image_ref_frame == "b1950") {
        _image_ref_frame = "fk4";
    } else if (_image_ref_frame == "j2000") {
        _image_ref_frame = "fk5";
    }

    if (pixel_coord) {
        _file_ref_frame = "image";
    } else {
        _file_ref_frame = _image_ref_frame;
    }

    AddHeader();
}

void Ds9ImportExport::InitGlobalProperties() {
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

void Ds9ImportExport::AddRegionNames() {
    _region_names[CARTA::RegionType::POINT] = "point";
    _region_names[CARTA::RegionType::RECTANGLE] = "box";
    _region_names[CARTA::RegionType::POLYGON] = "polygon";
    _region_names[CARTA::RegionType::ANNPOINT] = "# point";
    _region_names[CARTA::RegionType::ANNLINE] = "# line";
    _region_names[CARTA::RegionType::ANNPOLYLINE] = "# polyline";
    _region_names[CARTA::RegionType::ANNRECTANGLE] = "# box";
    _region_names[CARTA::RegionType::ANNELLIPSE] = "# ellipse";
    _region_names[CARTA::RegionType::ANNPOLYGON] = "# polygon";
    _region_names[CARTA::RegionType::ANNVECTOR] = "# vector";
    _region_names[CARTA::RegionType::ANNTEXT] = "# text";
}

// Public: for exporting regions

bool Ds9ImportExport::AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
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

bool Ds9ImportExport::AddExportRegion(const CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
    // Add region using Quantities
    float angle = rotation.get("deg").getValue(); // from LCRegion "theta" value in radians

    std::string region_line;
    if (_pixel_coord || _file_ref_frame.empty()) {
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

bool Ds9ImportExport::ExportRegions(std::string& filename, std::string& error) {
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

bool Ds9ImportExport::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    // Print regions to DS9 file lines in vector
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    contents = _export_regions;
    return true;
}

// Process file import

void Ds9ImportExport::ProcessFileLines(std::vector<std::string>& lines) {
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

void Ds9ImportExport::InitDs9CoordMap() {
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

bool Ds9ImportExport::IsDs9CoordSysKeyword(std::string& input_line) {
    // Check if region file line is coordinate in map
    std::string input_lower(input_line);
    std::transform(input_line.begin(), input_line.end(), input_lower.begin(), ::tolower); // convert to lowercase
    return (_coord_map.find(input_lower) != _coord_map.end());
}

bool Ds9ImportExport::SetFileReferenceFrame(std::string& ds9_coord) {
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

void Ds9ImportExport::SetImageReferenceFrame() {
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

// Import regions into RegionState vector

void Ds9ImportExport::SetGlobals(std::string& global_line) {
    // Set global properties using file line parser
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(global_line, parameters, properties);
    _global_properties = properties;
}

RegionProperties Ds9ImportExport::SetRegion(std::string& region_definition) {
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
            std::string align;
            if (properties.find("align") != properties.end()) {
                align = properties["align"];
            }
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

RegionState Ds9ImportExport::ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation) {
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
        if (ConvertPointToPixels(_file_ref_frame, param_quantities, pixel_coords)) {
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

RegionState Ds9ImportExport::ImportCircleRegion(std::vector<std::string>& parameters, bool is_annotation) {
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

RegionState Ds9ImportExport::ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation) {
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
            if (ConvertPointToPixels(_file_ref_frame, center_coords, pixel_coords)) {
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

RegionState Ds9ImportExport::ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation) {
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
            if (ConvertPointToPixels(_file_ref_frame, center_coords, pixel_coords)) {
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

RegionState Ds9ImportExport::ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation) {
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
            if (ConvertPointToPixels(_file_ref_frame, point, pixel_coords)) {
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

RegionState Ds9ImportExport::ImportVectorRegion(std::vector<std::string>& parameters) {
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
            if (ConvertPointToPixels(_file_ref_frame, point_coords, pixel_coords)) {
                control_points.push_back(Message::Point(pixel_coords));
            } else {
                _import_errors.append("Failed to apply " + region_name + " to image.\n");
                return region_state;
            }

            // (length, angle) to (x2, y2)
            auto length = param_quantities[2];
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

RegionState Ds9ImportExport::ImportRulerRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    // Import ruler using parameters and properties.
    // Start with properties to get coordinate system
    region_style = ImportStyleParameters(CARTA::RegionType::ANNRULER, properties);
    std::string coordinate_system;
    if (properties.find("ruler") != properties.end()) {
        ImportRulerStyle(properties["ruler"], coordinate_system);
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

RegionState Ds9ImportExport::ImportCompassRegion(
    std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style) {
    // Import compass using parameters and properties.
    // Start with properties to get coordinate system
    region_style = ImportStyleParameters(CARTA::RegionType::ANNCOMPASS, properties);
    std::string coordinate_system;
    if (properties.find("compass") != properties.end()) {
        ImportCompassStyle(properties["compass"], coordinate_system, region_style.mutable_annotation_style());
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

CARTA::RegionStyle Ds9ImportExport::ImportStyleParameters(
    CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties) {
    // Get style params from properties
    CARTA::RegionStyle region_style;
    CARTA::AnnotationStyle* annotation_style(nullptr);
    bool is_annotation = (region_type > CARTA::RegionType::POLYGON);
    if (is_annotation) {
        annotation_style = region_style.mutable_annotation_style();
    }

    // name for most regions, or text for text region
    if (properties.find("text") != properties.end()) {
        auto name = properties["text"];
        if (!name.empty() && name.front() == '{' && name.back() == '}') {
            name = name.substr(1, name.length() - 2);
        }

        // "text" property used for text not name
        if (region_type == CARTA::RegionType::ANNTEXT) {
            annotation_style->set_text_label0(name);
        } else {
            region_style.set_name(name);
        }
    }

    // color
    if (properties.find("color") != properties.end()) {
        region_style.set_color(FormatColor(properties["color"]));
    } else if (_global_properties.find("color") != _global_properties.end()) {
        region_style.set_color(FormatColor(_global_properties["color"]));
    } else {
        region_style.set_color(REGION_COLOR);
    }

    // line width
    int line_width(REGION_LINE_WIDTH), linewidth;
    if (properties.find("width") != properties.end() && StringToInt(properties["width"], linewidth)) {
        line_width = linewidth;
    } else if (_global_properties.find("width") != _global_properties.end() && StringToInt(properties["width"], linewidth)) {
        line_width = linewidth;
    }
    region_style.set_line_width(line_width);

    // line dash
    std::string dashlist;
    if (properties.find("dash") != properties.end() && (properties["dash"] == "1") && properties.find("dashlist") != properties.end()) {
        dashlist = properties["dashlist"];
    } else if (_global_properties.find("dash") != _global_properties.end() && (_global_properties["dash"] == "1") &&
               _global_properties.find("dashlist") != _global_properties.end()) {
        dashlist = _global_properties["dashlist"];
    }

    if (!dashlist.empty()) {
        // Convert string to int values
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

void Ds9ImportExport::ImportPointStyleParameters(
    std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    // DS9 combines parameters in one string
    if (properties.find("point") == properties.end()) {
        return;
    }
    std::vector<std::string> params;
    SplitString(properties["point"], ' ', params);
    int param_index(0), point_width;

    while (param_index < params.size()) {
        auto param = params[param_index++];

        if (StringToInt(param, point_width)) {
            annotation_style->set_point_width(point_width);
        } else {
            // Set point shape
            bool fill(false);
            if (properties.find("fill") != properties.end()) {
                fill = (properties["fill"] == "1" ? true : false);
            }

            auto point_ann_shape = CARTA::PointAnnotationShape::SQUARE;
            if (param == "circle") {
                point_ann_shape = (fill ? CARTA::PointAnnotationShape::CIRCLE : CARTA::PointAnnotationShape::CIRCLE_LINED);
            } else if (param == "box") {
                point_ann_shape = (fill ? CARTA::PointAnnotationShape::SQUARE : CARTA::PointAnnotationShape::BOX);
            } else if (param == "diamond") {
                point_ann_shape = (fill ? CARTA::PointAnnotationShape::DIAMOND : CARTA::PointAnnotationShape::DIAMOND_LINED);
            } else if (param == "cross") {
                point_ann_shape = CARTA::PointAnnotationShape::CROSS;
            } else if (param == "x") {
                point_ann_shape = CARTA::PointAnnotationShape::X;
            }
            annotation_style->set_point_shape(point_ann_shape);
        }
    }
}

void Ds9ImportExport::ImportFontStyleParameters(
    std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    // DS9 combines parameters in one string
    std::string font_settings;

    if (properties.find("font") != properties.end()) {
        font_settings = properties["font"];
    } else if (_global_properties.find("font") != _global_properties.end()) {
        font_settings = _global_properties["font"];
    }

    if (!font_settings.empty()) {
        std::vector<std::string> font_parameters;
        SplitString(font_settings, ' ', font_parameters);
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

bool Ds9ImportExport::ParamToQuantity(
    std::string& param, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity) {
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

bool Ds9ImportExport::Ds9ToCasacoreUnit(std::string& parameter, const std::string& region_type) {
    // Replace DS9 unit with casacore::Quantity unit in parameter string for readQuantity
    // Returns whether valid ds9 parameter
    bool valid(false);
    std::string error_prefix(region_type + " invalid parameter ");

    // use stod to find index of unit in string (after numeric value)
    size_t idx;
    try {
        double val = stod(parameter, &idx); // string to double
    } catch (std::invalid_argument& err) {
        std::string invalid_arg(error_prefix + parameter + ", not a numeric value.\n");
        _import_errors.append(invalid_arg);
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
                _import_errors.append(invalid_unit);
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
                _import_errors.append(invalid_unit);
            }
        }
    }
    return valid;
}

void Ds9ImportExport::ConvertTimeFormatToAngle(std::string& parameter) {
    // If parameter is in sexagesimal format dd:mm::ss.ssss, convert to angle format dd.mm.ss.ssss for readQuantity
    for (std::string::iterator it = parameter.begin(); it != parameter.end(); ++it) {
        if (*it == ':') {
            *it = '.';
        }
    }
}

// For export

void Ds9ImportExport::AddHeader() {
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

std::string Ds9ImportExport::AddExportRegionPixel(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
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

std::string Ds9ImportExport::AddExportRegionWorld(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
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
                if (ConvertPointToPixels(dir_frame, point0, point0_pix) && ConvertPointToPixels(dir_frame, point1, point1_pix)) {
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

void Ds9ImportExport::ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
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

void Ds9ImportExport::ExportTextboxStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    // Add region name and alignment
    if (!region_style.name().empty()) {
        region_line += fmt::format(" text={{{}}}", region_style.name());
    }
    region_line += fmt::format(" align={}\n", _text_positions[region_style.annotation_style().text_position()]);
}

void Ds9ImportExport::ExportFontParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
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

void Ds9ImportExport::ExportAnnotationStyleParameters(
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

void Ds9ImportExport::ExportAnnPointParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    std::string point_shape("circle");
    bool fill(false);

    switch (region_style.annotation_style().point_shape()) {
        case CARTA::PointAnnotationShape::SQUARE:
            point_shape = "box";
            fill = true;
            break;
        case CARTA::PointAnnotationShape::BOX:
            point_shape = "box";
            break;
        case CARTA::PointAnnotationShape::CIRCLE:
            point_shape = "circle";
            fill = true;
            break;
        case CARTA::PointAnnotationShape::CIRCLE_LINED:
            point_shape = "circle";
            break;
        case CARTA::PointAnnotationShape::DIAMOND:
            point_shape = "diamond";
            fill = true;
            break;
        case CARTA::PointAnnotationShape::DIAMOND_LINED:
            point_shape = "diamond";
            break;
        case CARTA::PointAnnotationShape::CROSS:
            point_shape = "cross";
            break;
        case CARTA::PointAnnotationShape::X:
            point_shape = "x";
            break;
    }

    auto point_size = region_style.annotation_style().point_width();
    region_line += fmt::format(" point={} {}", point_shape, point_size);

    if (fill) {
        region_line += " fill=1";
    }
}
