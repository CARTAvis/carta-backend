/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfImportExport.cc: import and export regions in CRTF format

#include "CrtfImportExport.h"

#include <carta-protobuf/enums.pb.h>
#include <casacore/casa/Quanta/QMath.h>
#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnLine.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnPolyline.h>
#include <imageanalysis/Annotations/AnnRectBox.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>
#include <imageanalysis/Annotations/AnnSymbol.h>

#include <iomanip>

#include "../Logger/Logger.h"

using namespace carta;

CrtfImportExport::CrtfImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape,
    int stokes_axis, int file_id, const std::string& file, bool file_is_filename)
    : RegionImportExport(image_coord_sys, image_shape, file_id), _stokes_axis(stokes_axis) {
    // Import regions from CRTF region file
    // Set delimiters for parsing file lines
    SetParserDelim(" ,[]");

    try {
        std::vector<std::string> file_lines = ReadRegionFile(file, file_is_filename);
        ProcessFileLines(file_lines);
    } catch (const casacore::AipsError& err) {
        // Note exception and quit.
        casacore::String error = err.getMesg().before("at File");
        error = error.before("thrown by");
        _import_errors = error;
    }
}

CrtfImportExport::CrtfImportExport(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis)
    : RegionImportExport(image_coord_sys, image_shape), _stokes_axis(stokes_axis) {
    // Export regions; will add each region to RegionTextList
    _region_list = casa::RegionTextList(*image_coord_sys, image_shape);
}

// Public: for exporting regions

bool CrtfImportExport::AddExportRegion(const RegionState& region_state, const RegionStyle& region_style) {
    // Add pixel region using RegionState
    std::vector<CARTA::Point> points = region_state.control_points;
    float angle = region_state.rotation;
    std::string region_line;

    // Print region parameters (pixel coordinates) to CRTF-format string
    switch (region_state.type) {
        case CARTA::RegionType::POINT: {
            // symbol [[x, y], .]
            region_line = fmt::format("symbol [[{:.4f}pix, {:.4f}pix], .]", points[0].x(), points[0].y());
            break;
        }
        case CARTA::RegionType::RECTANGLE: {
            if (angle == 0.0) {
                // centerbox [[x, y], [width, height]]
                region_line = fmt::format("centerbox [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix]]", points[0].x(), points[0].y(),
                    points[1].x(), points[1].y());
            } else {
                // rotbox [[x, y], [width, height], angle]
                region_line = fmt::format("rotbox [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", points[0].x(), points[0].y(),
                    points[1].x(), points[1].y(), angle);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE: {
            // ellipse [[x, y], [radius, radius], angle]
            region_line = fmt::format("ellipse [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", points[0].x(), points[0].y(),
                points[1].x(), points[1].y(), angle);
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON: {
            // e.g. poly [[x1, y1], [x2, y2], [x3, y3],...]
            region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix]", _region_names[region_state.type], points[0].x(), points[0].y());
            for (size_t i = 1; i < points.size(); ++i) {
                region_line += fmt::format(", [{:.4f}pix, {:.4f}pix]", points[i].x(), points[i].y());
            }
            region_line += "]";
            break;
        }
        default:
            break;
    }

    // Add to export region vector
    if (!region_line.empty()) {
        ExportStyleParameters(region_style, region_line);
        _export_regions.push_back(region_line);
        return true;
    }

    return false;
}

bool CrtfImportExport::ExportRegions(std::string& filename, std::string& error) {
    // Print regions to CRTF file
    if ((_region_list.nLines() == 0) && _export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    std::ofstream export_file(filename);
    if (_region_list.nLines() > 0) {
        try {
            // Includes header and Annotation region lines
            _region_list.print(export_file);
        } catch (const casacore::AipsError& err) {
            export_file.close();
            error = err.getMesg();
            return false;
        }
    } else {
        // Print header
        export_file << GetCrtfVersionHeader();
    }

    // With workarounds, may have combination of Annotation region lines and export region strings.
    // Print any region strings
    for (auto& region : _export_regions) {
        export_file << region << "\n";
    }

    export_file.close();
    return true;
}

bool CrtfImportExport::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    // Print regions to CRTF file lines in vector
    if ((_region_list.nLines() == 0) && _export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    // Print header
    contents.push_back(GetCrtfVersionHeader());

    if (_region_list.nLines() > 0) {
        try {
            for (unsigned int i = 0; i < _region_list.nLines(); ++i) {
                casa::AsciiAnnotationFileLine file_line = _region_list.lineAt(i);
                std::ostringstream export_stream;
                file_line.print(export_stream);
                contents.push_back(export_stream.str());
            }
        } catch (const casacore::AipsError& err) {
            error = err.getMesg();
            return false;
        }
    } else {
        for (auto& region : _export_regions) {
            contents.push_back(region);
        }
    }

    return true;
}

// Protected: for exporting regions in world coordinates

bool CrtfImportExport::AddExportRegion(CARTA::RegionType region_type, const RegionStyle& region_style,
    const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation) {
    // Create Annotation region from control point Quantities for export format
    if (control_points.empty()) {
        return false;
    }

    // Common AnnRegion parameters
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
    bool require_region(false); // can be outside image

    casa::AnnotationBase* ann_base(nullptr); // symbol, line, polyline
    casa::AnnRegion* ann_region(nullptr);    // all other regions
    try {
        switch (region_type) {
            case CARTA::RegionType::POINT: {
                casacore::Quantity x(control_points[0]);
                casacore::Quantity y(control_points[1]);
                ann_base = new casa::AnnSymbol(x, y, *_coord_sys, casa::AnnSymbol::POINT, stokes_types);
                break;
            }
            case CARTA::RegionType::LINE: {
                casacore::Quantity x1(control_points[0]);
                casacore::Quantity y1(control_points[1]);
                casacore::Quantity x2(control_points[2]);
                casacore::Quantity y2(control_points[3]);
                ann_base = new casa::AnnLine(x1, y1, x2, y2, *_coord_sys, stokes_types);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                casacore::Quantity cx(control_points[0]);
                casacore::Quantity cy(control_points[1]);
                casacore::Quantity xwidth(control_points[2]);
                casacore::Quantity ywidth(control_points[3]);
                if (rotation.getValue() == 0) {
                    ann_region = new casa::AnnCenterBox(cx, cy, xwidth, ywidth, *_coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    ann_region =
                        new casa::AnnRotBox(cx, cy, xwidth, ywidth, rotation, *_coord_sys, _image_shape, stokes_types, require_region);
                }
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                casacore::Quantity cx(control_points[0]);
                casacore::Quantity cy(control_points[1]);
                casacore::Quantity bmaj(control_points[2]);
                casacore::Quantity bmin(control_points[3]);
                casacore::Quantity rotangle(rotation.get("deg"));
                rotangle -= 90.0;
                if (rotangle.getValue() < 0.0) {
                    rotangle += 360.0;
                }
                ann_region = new casa::AnnEllipse(cx, cy, bmaj, bmin, rotangle, *_coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            case CARTA::RegionType::POLYGON:
            case CARTA::RegionType::POLYLINE: {
                // Points are in order x1, y1, x2, y2, etc.
                size_t npoints(control_points.size());
                casacore::Vector<casacore::Quantity> x_coords(npoints / 2), y_coords(npoints / 2);
                int index(0);
                for (size_t i = 0; i < npoints; i += 2) {
                    x_coords(index) = control_points[i];
                    y_coords(index++) = control_points[i + 1];
                }
                if (region_type == CARTA::RegionType::POLYGON) {
                    ann_region = new casa::AnnPolygon(x_coords, y_coords, *_coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    ann_region = new casa::AnnPolyline(x_coords, y_coords, *_coord_sys, _image_shape, stokes_types, require_region);
                }
                break;
            }
            default:
                break;
        }

        std::ostringstream oss;
        if (ann_region) {
            ann_region->setAnnotationOnly(false);
            ExportStyleParameters(region_style, ann_region);
            ann_region->print(oss);
            delete ann_region;
        }
        if (ann_base) {
            ExportStyleParameters(region_style, ann_base);
            ann_base->print(oss);
            delete ann_region;
        }

        // Create region string and add to export regions vector
        std::string region_line(oss.str());
        if (!region_line.empty()) {
            // Change "poly" to "polyline"
            if (region_type == CARTA::RegionType::POLYLINE) {
                region_line.insert(4, "line");
            }

            _export_regions.push_back(region_line);
        } else {
            spdlog::error("CRTF export error for region type {}", region_type);
            return false;
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }

    return true;
}

// Private: for importing regions

void CrtfImportExport::ProcessFileLines(std::vector<std::string>& lines) {
    // Import regions defined on each line of file
    casa::AnnotationBase::unitInit(); // enable "pix" unit

    for (auto& line : lines) {
        if (line.empty() || (line[0] == '#')) {
            continue; // ignore blank lines and comments
        }

        // Parse line
        std::vector<std::string> parameters;
        std::unordered_map<std::string, std::string> properties;
        ParseRegionParameters(line, parameters, properties);

        // Coordinate frame for world coordinates conversion
        std::string coord_frame = GetRegionDirectionFrame(properties);

        std::string region(parameters[0]);
        RegionState region_state;
        if (region == "symbol") {
            region_state = ImportAnnSymbol(parameters, coord_frame);
        } else if (region == "line") {
            region_state = ImportAnnPolygonLine(parameters, coord_frame);
        } else if (region.find("box") != std::string::npos) {
            // Handles "box", "centerbox", "rotbox"
            region_state = ImportAnnBox(parameters, coord_frame);
        } else if ((region == "ellipse") || (region == "circle")) {
            region_state = ImportAnnEllipse(parameters, coord_frame);
        } else if (region.find("poly") != std::string::npos) {
            region_state = ImportAnnPolygonLine(parameters, coord_frame);
        } else if (region == "global") {
            ImportGlobalParameters(properties);
        } else {
            _import_errors.append(region + " not supported.\n");
        }

        if (region_state.RegionDefined()) {
            // Set RegionStyle
            RegionStyle region_style = ImportStyleParameters(properties);

            // Set RegionProperties and add to list
            RegionProperties region_properties(region_state, region_style);
            _import_regions.push_back(region_properties);
        }
    }
}

void CrtfImportExport::ImportGlobalParameters(std::unordered_map<std::string, std::string>& properties) {
    // Import properties from global file line to map
    std::vector<std::string> global_keywords = {"coord", "range", "frame", "corr", "veltype", "restfreq", "linewidth", "linestyle",
        "symsize", "symthick", "color", "font", "fontsize", "fontstyle", "usetex", "label", "labelcolor", "labelpos", "labeloff"};

    for (auto& property : properties) {
        if (std::find(global_keywords.begin(), global_keywords.end(), property.first) != global_keywords.end()) {
            _global_properties[property.first] = property.second;
        }
    }
}

std::string CrtfImportExport::GetRegionDirectionFrame(std::unordered_map<std::string, std::string>& properties) {
    std::string dir_frame;

    if (properties.count("coord")) {
        dir_frame = properties["coord"];
    } else if (_global_properties.count("coord")) {
        dir_frame = _global_properties["coord"];
    } else if (_coord_sys->hasDirectionCoordinate()) {
        casacore::MDirection::Types mdir_type = _coord_sys->directionCoordinate().directionType();
        dir_frame = casacore::MDirection::showType(mdir_type);
    }

    return dir_frame;
}

RegionState CrtfImportExport::ImportAnnSymbol(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import AnnSymbol in pixel coordinates to RegionState
    RegionState region_state;

    if (parameters.size() >= 3) { // symbol x y, optional symbol shape
        // Convert string to Quantities
        casacore::Quantity x, y;
        try {
            casacore::readQuantity(x, parameters[1]);
            casacore::readQuantity(y, parameters[2]);
        } catch (const casacore::AipsError& err) {
            spdlog::error("symbol import Quantity error: {}", err.getMesg());
            _import_errors.append("symbol parameters invalid.\n");
            return region_state;
        }

        try {
            // Convert to pixels
            std::vector<casacore::Quantity> point;
            point.push_back(x);
            point.push_back(y);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(coord_frame, point, pixel_coords)) {
                // Set control points
                std::vector<CARTA::Point> control_points;
                control_points.push_back(Message::Point(pixel_coords));

                // Set RegionState
                CARTA::RegionType type(CARTA::RegionType::POINT);
                float rotation(0.0);
                region_state = RegionState(_file_id, type, control_points, rotation);
            } else {
                spdlog::error("symbol import conversion to pixel failed");
                _import_errors.append("symbol import failed.\n");
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("symbol import error: {}", err.getMesg());
            _import_errors.append("symbol import failed.\n");
        }
    } else {
        _import_errors.append("symbol syntax invalid.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import Annotation box in pixel coordinates to RegionState
    RegionState region_state;

    if (parameters.size() >= 5) {
        // [box blcx blcy trcx trcy], [centerbox cx cy width height], or [rotbox cx cy width height angle]
        std::string region(parameters[0]);

        // Use parameters to get control points and rotation
        std::vector<CARTA::Point> control_points;
        float rotation(0.0);
        if (!GetBoxControlPoints(parameters, coord_frame, control_points, rotation)) {
            return region_state;
        }

        // Create RegionState and add to vector
        CARTA::RegionType type(CARTA::RegionType::RECTANGLE);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("box syntax invalid.\n");
    }

    return region_state;
}

RegionState CrtfImportExport::ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import AnnEllipse in pixel coordinates to RegionState
    RegionState region_state;
    std::string region(parameters[0]);

    if (parameters.size() >= 4) {
        // [ellipse cx cy bmaj bmin angle] or [circle cx cy r]
        casacore::Quantity cx, cy, p3, p4, p5;
        float rotation(0.0);
        try {
            // Center point
            casacore::readQuantity(cx, parameters[1]);
            casacore::readQuantity(cy, parameters[2]);
            casacore::readQuantity(p3, parameters[3]);

            if (region == "ellipse") {
                casacore::readQuantity(p4, parameters[4]);

                // rotation
                casacore::readQuantity(p5, parameters[5]);
                rotation = p5.get("deg").getValue();
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import Quantity error: {}", region, err.getMesg());
            _import_errors.append(region + " parameters invalid.\n");
        }

        try {
            // Convert to pixels
            std::vector<casacore::Quantity> point;
            point.push_back(cx);
            point.push_back(cy);
            casacore::Vector<casacore::Double> pixel_coords;
            if (ConvertPointToPixels(coord_frame, point, pixel_coords)) {
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
                CARTA::RegionType type(CARTA::RegionType::ELLIPSE);
                region_state = RegionState(_file_id, type, control_points, rotation);
            } else {
                spdlog::error("{} import conversion to pixel failed", region);
                _import_errors.append(region + " import failed.\n");
            }
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import error: {}", region, err.getMesg());
            _import_errors.append(region + " import failed.\n");
        }
    } else {
        _import_errors.append(region + " syntax invalid.\n");
    }

    return region_state;
}

RegionState CrtfImportExport::ImportAnnPolygonLine(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import AnnPolygon, AnnPolyline, or AnnLine in pixel coordinates to RegionState
    RegionState region_state;
    std::string region(parameters[0]);

    if (parameters.size() >= 5) {
        // poly x1 y1 x2 y2 x3 y3 ...
        // polyline x1 y1 x2 y2 x3 y3...
        // line x1 y1 x2 y2

        // poly check: at least 3 points
        if ((region.find("poly") != std::string::npos) && (parameters.size() < 7)) {
            _import_errors.append(region + " syntax invalid.\n");
            return region_state;
        }

        try {
            std::vector<CARTA::Point> control_points;
            // Convert parameters in x,y pairs
            for (size_t i = 1; i < parameters.size(); i += 2) {
                casacore::Quantity x, y;
                casacore::readQuantity(x, parameters[i]);
                casacore::readQuantity(y, parameters[i + 1]);

                // Convert to pixels
                std::vector<casacore::Quantity> point;
                point.push_back(x);
                point.push_back(y);
                casacore::Vector<casacore::Double> pixel_coords;
                if (ConvertPointToPixels(coord_frame, point, pixel_coords)) {
                    // Set control points
                    control_points.push_back(Message::Point(pixel_coords));
                } else {
                    spdlog::error("{} import conversion to pixel failed", region);
                    _import_errors.append(region + " import failed.\n");
                    return region_state;
                }
            }

            // Set type
            CARTA::RegionType type(CARTA::RegionType::POLYGON);
            if (region == "line") {
                type = CARTA::RegionType::LINE;
            } else if (region == "polyline") {
                type = CARTA::RegionType::POLYLINE;
            }

            // Create RegionState and add to vector
            float rotation(0.0);
            region_state = RegionState(_file_id, type, control_points, rotation);
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import error: {}", region, err.getMesg());
            _import_errors.append(region + " import failed.\n");
        }
    } else {
        _import_errors.append(region + " syntax invalid.\n");
    }

    return region_state;
}

RegionStyle CrtfImportExport::ImportStyleParameters(std::unordered_map<std::string, std::string>& properties) {
    // Get RegionStyle parameters from properties map
    RegionStyle style;

    // name
    if (properties.count("label")) {
        style.name = properties["label"];
    }

    // color
    std::string import_color("green"); // CRTF default
    if (properties.count("color")) {
        import_color = FormatColor(properties["color"]);
    } else if (_global_properties.count("color")) {
        import_color = FormatColor(_global_properties["color"]);
    }
    if (std::strtoul(import_color.c_str(), nullptr, 16)) {
        // add prefix if hex
        import_color = "#" + import_color;
    }
    style.color = import_color;

    // linewidth
    if (properties.count("linewidth")) {
        style.line_width = std::stoi(properties["linewidth"]);
    } else if (_global_properties.count("linewidth")) {
        style.line_width = std::stoi(_global_properties["linewidth"]);
    } else {
        style.line_width = 1; // CRTF default
    }

    // linestyle
    std::string linestyle("-"); // solid
    if (properties.count("linestyle")) {
        linestyle = properties["linestyle"];
    } else if (_global_properties.count("linestyle")) {
        linestyle = _global_properties["linestyle"];
    }
    if (linestyle == "-") { // solid line
        style.dash_list = {0, 0};
    } else {
        style.dash_list = {REGION_DASH_LENGTH, REGION_DASH_LENGTH};
    }

    return style;
}

// Private import helpers for rectangles

bool CrtfImportExport::GetBoxControlPoints(std::string& box_definition, std::vector<CARTA::Point>& control_points, float& rotation) {
    // Parse box definition to get parameters, then get CARTA rectangle control points
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(box_definition, parameters, properties);

    auto coord_frame = GetRegionDirectionFrame(properties);

    return GetBoxControlPoints(parameters, coord_frame, control_points, rotation);
}

bool CrtfImportExport::GetBoxControlPoints(
    std::vector<std::string>& parameters, std::string& region_frame, std::vector<CARTA::Point>& control_points, float& rotation) {
    // Use box parameters to determine CARTA control points (center and size) and rotation.
    // Used for:
    // - import rotbox (always a polygon)
    // - import rectangle that forms a polygon not [blc, trc] rectangle in wcs
    // - import rectangle to linear coord sys (must be pixel)
    // - import when CRTF file contains "polyline" not supported by casa
    // Returns false if conversion from string to Quantity fails
    std::string region(parameters[0]);
    casacore::Quantity p1, p2, p3, p4;
    try {
        // Convert parameters to Quantity:
        casacore::readQuantity(p1, parameters[1]);
        casacore::readQuantity(p2, parameters[2]);
        casacore::readQuantity(p3, parameters[3]);
        casacore::readQuantity(p4, parameters[4]);

        if (region == "rotbox") {
            casacore::Quantity angle;
            casacore::readQuantity(angle, parameters[5]);
            rotation = angle.get("deg").getValue();
        } else {
            rotation = 0.0;
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("{} import Quantity error: {}", region, err.getMesg());
        return false;
    }

    if ((region == "rotbox") || (region == "centerbox")) {
        // cx, cy, width, height
        return GetCenterBoxPoints(region, p1, p2, p3, p4, region_frame, control_points);
    } else {
        // blc_x, blc_y, trc_x, trc_y
        return GetRectBoxPoints(p1, p2, p3, p4, region_frame, control_points);
    }

    return false;
}

bool CrtfImportExport::GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy,
    casacore::Quantity& width, casacore::Quantity& height, std::string& region_frame, std::vector<CARTA::Point>& control_points) {
    // Convert coordinates to pixel, return CARTA::Rectangle control points
    try {
        // Convert center point cx, cy to pixel
        std::vector<casacore::Quantity> centerpoint;
        centerpoint.push_back(cx);
        centerpoint.push_back(cy);
        casacore::Vector<casacore::Double> pixel_coords;
        if (ConvertPointToPixels(region_frame, centerpoint, pixel_coords)) {
            // Set control points
            control_points.push_back(Message::Point(pixel_coords));
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

bool CrtfImportExport::GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx,
    casacore::Quantity& trcy, std::string& region_frame, std::vector<CARTA::Point>& control_points) {
    // Use corners to calculate centerbox parameters
    bool converted(false);
    try {
        // Quantity math will fail if non-compatible units
        casacore::Quantity cx = (blcx + trcx) / 2.0;
        casacore::Quantity cy = (blcy + trcy) / 2.0;
        casacore::Quantity width = (trcx - blcx);
        casacore::Quantity height = (trcy - blcy);
        converted = GetCenterBoxPoints("box", cx, cy, width, height, region_frame, control_points);
    } catch (const casacore::AipsError& err) {
        spdlog::error("box import Quantity error: {}", err.getMesg());
    }

    return converted;
}

// Private: for exporting regions

void CrtfImportExport::ExportStyleParameters(const RegionStyle& region_style, std::string& region_line) {
    // Add standard CRTF keywords with default values or optional label to region_line
    std::ostringstream oss;
    // linewidth
    oss << " linewidth=" << std::to_string(region_style.line_width) << ", ";

    // linestyle
    casa::AnnotationBase::LineStyle style(casa::AnnotationBase::SOLID);
    if (!region_style.dash_list.empty() && (region_style.dash_list[0] != 0)) {
        style = casa::AnnotationBase::DASHED;
    }
    oss << "linestyle=" << casa::AnnotationBase::lineStyleToString(style) << ", ";

    // symsize, symthick
    oss << "symsize=" << std::to_string(casa::AnnotationBase::DEFAULT_SYMBOLSIZE) << ", ";
    oss << "symthick=" << std::to_string(casa::AnnotationBase::DEFAULT_SYMBOLTHICKNESS) << ", ";

    // color: lowercase with no leading #
    std::string hex_color = region_style.color;
    if (hex_color[0] == '#') {
        hex_color = hex_color.substr(1);
    }
    std::transform(hex_color.begin(), hex_color.end(), hex_color.begin(), ::tolower);
    oss << "color=" << hex_color << ", ";

    // font, fontsize, fontstyle, usetex
    oss << "font=\"" << casa::AnnotationBase::DEFAULT_FONT << "\", ";
    oss << "fontsize=" << std::to_string(casa::AnnotationBase::DEFAULT_FONTSIZE) << ", ";
    oss << "fontstyle=" << casa::AnnotationBase::fontStyleToString(casa::AnnotationBase::DEFAULT_FONTSTYLE) << ", ";
    oss << "usetex=" << (casa::AnnotationBase::DEFAULT_USETEX ? "true" : "false");

    // label if set
    if (!region_style.name.empty()) {
        oss << ", label=\"" << region_style.name << "\", ";
        oss << "labelcolor=green, ";
        oss << "labelpos=" << casa::AnnotationBase::DEFAULT_LABELPOS;
    }

    region_line.append(oss.str());
}

void CrtfImportExport::ExportStyleParameters(const RegionStyle& region_style, casa::AnnotationBase* region) {
    // Set region style parameters in region
    // label
    if (!region_style.name.empty()) {
        region->setLabel(region_style.name);
        region->setLabelColor(casa::AnnotationBase::DEFAULT_LABELCOLOR);
        region->setLabelPosition(casa::AnnotationBase::DEFAULT_LABELPOS);
    }

    // color: remove leading '#', keep lower case
    std::string color = region_style.color;
    if (color[0] == '#') {
        color = color.substr(1);
    }
    region->setColor(color);

    // linewidth
    region->setLineWidth(region_style.line_width);

    // linestyle
    casa::AnnotationBase::LineStyle line_style(casa::AnnotationBase::SOLID);
    if (!region_style.dash_list.empty() && region_style.dash_list[0] != 0) {
        line_style = casa::AnnotationBase::DASHED;
    }
    region->setLineStyle(line_style);
}

// Private: export helpers

casacore::Vector<casacore::Stokes::StokesTypes> CrtfImportExport::GetStokesTypes() {
    // convert ints to stokes types in vector
    casacore::Vector<casacore::Int> istokes;
    if (_coord_sys->hasPolarizationCoordinate()) {
        istokes = _coord_sys->stokesCoordinate().stokes();
    }

    if (istokes.empty() && (_stokes_axis >= 0)) {
        // make istokes vector from stokes axis size
        unsigned int nstokes(_image_shape(_stokes_axis));
        istokes.resize(nstokes);
        for (unsigned int i = 0; i < nstokes; ++i) {
            istokes(i) = i + 1;
        }
    }

    // convert Int to StokesTypes
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types(istokes.size());
    for (size_t i = 0; i < istokes.size(); ++i) {
        stokes_types(i) = casacore::Stokes::type(istokes(i));
    }
    return stokes_types;
}

std::string CrtfImportExport::GetCrtfVersionHeader() {
    // First line indicates CRTF region file and version
    std::ostringstream header;
    header << "#CRTFv" << casa::RegionTextParser::CURRENT_VERSION;
    header << " CASA Region Text Format version " << casa::RegionTextParser::CURRENT_VERSION << std::endl;
    return header.str();
}
