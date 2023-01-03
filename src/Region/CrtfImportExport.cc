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
#include <imageanalysis/Annotations/AnnText.h>
#include <imageanalysis/Annotations/AnnVector.h>

#include "Logger/Logger.h"
#include "Util/String.h"

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
    // Export regions; will add each region to _export_regions list
    AddExportRegionNames();
}

void CrtfImportExport::AddExportRegionNames() {
    _region_names[CARTA::RegionType::POINT] = "symbol";
    _region_names[CARTA::RegionType::RECTANGLE] = "centerbox";
    _region_names[CARTA::RegionType::POLYGON] = "poly";
    _region_names[CARTA::RegionType::ANNPOINT] = "ann symbol";
    _region_names[CARTA::RegionType::ANNLINE] = "ann line";
    _region_names[CARTA::RegionType::ANNPOLYLINE] = "ann polyline";
    _region_names[CARTA::RegionType::ANNRECTANGLE] = "ann centerbox";
    _region_names[CARTA::RegionType::ANNELLIPSE] = "ann ellipse";
    _region_names[CARTA::RegionType::ANNPOLYGON] = "ann poly";
    _region_names[CARTA::RegionType::ANNVECTOR] = "vector";
    _region_names[CARTA::RegionType::ANNTEXT] = "text";
}

// Public: for exporting regions

bool CrtfImportExport::AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
    // Add pixel region using RegionState
    auto region_type = region_state.type;
    std::vector<CARTA::Point> points = region_state.control_points;
    float angle = region_state.rotation;
    std::string region_line;

    // Print region parameters (pixel coordinates) to CRTF-format string
    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT:
        case CARTA::RegionType::ANNTEXT: {
            // symbol [[x, y], .] or text [[x, y], '{name}']
            std::string symbol_or_text(".");

            if (region_style.has_annotation_style()) {
                if (region_type == CARTA::ANNTEXT) {
                    // Match style of imageanalysis AnnText::print() - double quotes
                    symbol_or_text = "\"" + region_style.annotation_style().text_label0() + "\"";
                } else {
                    symbol_or_text = GetAnnSymbolCharacter(region_style.annotation_style().point_shape());
                }
            }

            region_line =
                fmt::format("{} [[{:.4f}pix, {:.4f}pix], {}]", _region_names[region_type], points[0].x(), points[0].y(), symbol_or_text);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE: {
            if (angle == 0.0) {
                // centerbox [[x, y], [width, height]]
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix]]", _region_names[region_type], points[0].x(),
                    points[0].y(), points[1].x(), points[1].y());
            } else {
                // rotbox [[x, y], [width, height], angle]
                std::string name = region_type == CARTA::RegionType::RECTANGLE ? "rotbox" : "ann rotbox";
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", name, points[0].x(), points[0].y(),
                    points[1].x(), points[1].y(), angle);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE: {
            // ellipse [[x, y], [radius, radius], angle] OR circle[[x, y], r] OR "## compass[[x, y], length]"
            if (points[1].x() == points[1].y()) { // bmaj == bmin
                std::string name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "ann circle");
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], {:.4f}pix]", name, points[0].x(), points[0].y(), points[1].x());
            } else {
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", _region_names[region_type],
                    points[0].x(), points[0].y(), points[1].x(), points[1].y(), angle);
            }
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            // # compass [[x, y], length]
            region_line = fmt::format(
                "{} [[{:.4f}pix, {:.4f}pix], {:.4f}pix]", _region_names[region_type], points[0].x(), points[0].y(), points[1].x());
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNVECTOR:
        case CARTA::RegionType::ANNRULER: {
            // e.g. poly [[x1, y1], [x2, y2], [x3, y3],...]
            region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix]", _region_names[region_type], points[0].x(), points[0].y());
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

        if (region_type == CARTA::RegionType::ANNRULER) {
            auto coord_sys = GetAnnotationCoordinateSystem();
            std::string unit = (coord_sys == "image" || coord_sys == "linear" ? "image" : "degrees");
            region_line += fmt::format(" ruler={} {}", coord_sys, unit);
        } else if (region_type == CARTA::RegionType::ANNCOMPASS) {
            ExportAnnCompassStyle(region_style, GetAnnotationCoordinateSystem(), region_line);
        }

        _export_regions.push_back(region_line);
        return true;
    }

    return false;
}

bool CrtfImportExport::ExportRegions(std::string& filename, std::string& error) {
    // Print regions to CRTF file
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    std::ofstream export_file(filename);
    // Print header
    export_file << GetCrtfVersionHeader();

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
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    // Print header
    contents.push_back(GetCrtfVersionHeader());
    for (auto& region : _export_regions) {
        contents.push_back(region);
    }

    return true;
}

// Protected: for exporting regions in world coordinates

bool CrtfImportExport::AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
    // Create casa::AnnotationBase region from control point Quantities to print in export format
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
            case CARTA::RegionType::POINT:
            case CARTA::RegionType::ANNPOINT:
            case CARTA::RegionType::ANNTEXT: {
                casacore::Quantity x(control_points[0]);
                casacore::Quantity y(control_points[1]);
                casa::AnnSymbol::Symbol symbol(casa::AnnSymbol::POINT);
                std::string text("");

                if (region_style.has_annotation_style()) {
                    if (region_type == CARTA::ANNTEXT) {
                        text = region_style.annotation_style().text_label0();
                    } else {
                        symbol = GetAnnSymbol(region_style.annotation_style().point_shape());
                    }
                }

                if (region_type == CARTA::ANNTEXT) {
                    ann_base = new casa::AnnText(x, y, *_coord_sys, text, stokes_types);
                } else {
                    ann_base = new casa::AnnSymbol(x, y, *_coord_sys, symbol, stokes_types);
                }
                break;
            }
            case CARTA::RegionType::LINE:
            case CARTA::RegionType::ANNLINE:
            case CARTA::RegionType::ANNVECTOR:
            case CARTA::RegionType::ANNRULER: {
                casacore::Quantity x1(control_points[0]);
                casacore::Quantity y1(control_points[1]);
                casacore::Quantity x2(control_points[2]);
                casacore::Quantity y2(control_points[3]);

                if (region_type == CARTA::ANNVECTOR) {
                    ann_base = new casa::AnnVector(x1, y1, x2, y2, *_coord_sys, stokes_types);
                } else {
                    ann_base = new casa::AnnLine(x1, y1, x2, y2, *_coord_sys, stokes_types);
                }
                break;
            }
            case CARTA::RegionType::RECTANGLE:
            case CARTA::RegionType::ANNRECTANGLE: {
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
            case CARTA::RegionType::ELLIPSE:
            case CARTA::RegionType::ANNELLIPSE:
            case CARTA::RegionType::ANNCOMPASS: {
                casacore::Quantity cx(control_points[0]);
                casacore::Quantity cy(control_points[1]);
                casacore::Quantity bmaj(control_points[2]);
                casacore::Quantity bmin(control_points[3]);
                casacore::Quantity rotangle(rotation.get("deg"));
                rotangle -= 90.0;
                if (rotangle.getValue() < 0.0) {
                    rotangle += 360.0;
                }
                if ((region_type == CARTA::ELLIPSE || region_type == CARTA::ANNELLIPSE) && (bmaj != bmin)) {
                    ann_region =
                        new casa::AnnEllipse(cx, cy, bmaj, bmin, rotangle, *_coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    ann_region = new casa::AnnCircle(cx, cy, bmaj, *_coord_sys, _image_shape, stokes_types, require_region);
                }
                break;
            }
            case CARTA::RegionType::POLYGON:
            case CARTA::RegionType::POLYLINE:
            case CARTA::RegionType::ANNPOLYGON:
            case CARTA::RegionType::ANNPOLYLINE: {
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
            ann_region->setAnnotationOnly(region_type > CARTA::RegionType::POLYGON);
            ExportStyleParameters(region_style, ann_region);
            ann_region->print(oss);
            delete ann_region;
        }
        if (ann_base) {
            ExportStyleParameters(region_style, ann_base);
            ann_base->print(oss);
            delete ann_base;
        }

        // Create region string and add to export regions vector
        std::string region_line(oss.str());

        // Bug in imageanalysis: exports misspelled fontstyle, imports different string!
        if (region_line.find("itatlic_bold") != std::string::npos) {
            region_line.replace(region_line.find("itatlic_bold"), 12, "bold-italic", 11);
        }

        if (!region_line.empty()) {
            // "Fix" unsupported region types
            switch (region_type) {
                case CARTA::RegionType::POLYLINE:
                    region_line.insert(4, "line"); // "poly" -> "polyline"
                    break;
                case CARTA::RegionType::ANNPOLYLINE:
                    region_line.insert(8, "line"); // "ann poly" -> "ann polyline"
                    break;
                case CARTA::RegionType::ANNPOINT:
                case CARTA::RegionType::ANNLINE:
                    region_line = "ann " + region_line; // add explicit "ann"
                    break;
                case CARTA::RegionType::ANNRULER: {
                    region_line.replace(0, 4, _region_names[region_type]); // "line" -> "# ruler"
                    auto coord_sys = GetAnnotationCoordinateSystem();
                    std::string unit = (coord_sys == "image" || coord_sys == "linear" ? "image" : "degrees");
                    region_line += fmt::format(" ruler={} {}", coord_sys, unit);
                    break;
                }
                case CARTA::RegionType::ANNCOMPASS: {
                    region_line.replace(0, 10, _region_names[region_type]); // "ann circle" -> "# compass"
                    ExportAnnCompassStyle(region_style, GetAnnotationCoordinateSystem(), region_line);
                    break;
                }
                default:
                    break;
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

// Private: for import and export

std::string CrtfImportExport::GetImageDirectionFrame() {
    std::string dir_frame;
    if (_coord_sys->hasDirectionCoordinate()) {
        casacore::MDirection::Types mdir_type = _coord_sys->directionCoordinate().directionType();
        dir_frame = casacore::MDirection::showType(mdir_type);
    }
    return dir_frame;
}

// Private: for importing regions

void CrtfImportExport::ProcessFileLines(std::vector<std::string>& lines) {
    // Import regions defined on each line of file
    casa::AnnotationBase::unitInit(); // enable "pix" unit

    for (auto& line : lines) {
        if (line.empty()) {
            continue;
        }

        if (line[0] == '#' && line.find("# compass") == std::string::npos && line.find("# ruler") == std::string::npos) {
            continue;
        }

        // Parse line
        std::vector<std::string> parameters;
        std::unordered_map<std::string, std::string> properties;
        ParseRegionParameters(line, parameters, properties);

        // Coordinate frame for world coordinates conversion
        RegionState region_state;
        auto region = parameters[0] == "ann" ? parameters[1] : parameters[0];
        auto coord_frame = GetRegionDirectionFrame(properties);

        if ((region == "symbol") || (region == "text")) {
            region_state = ImportAnnSymbol(parameters, coord_frame);
        } else if ((region == "line") || (region == "vector") || (region == "ruler")) {
            region_state = ImportAnnPoly(parameters, coord_frame);
        } else if (region.find("box") != std::string::npos) { // "box", "centerbox", "rotbox"
            region_state = ImportAnnBox(parameters, coord_frame);
        } else if ((region == "ellipse") || (region == "circle") || (region == "compass")) {
            region_state = ImportAnnEllipse(parameters, coord_frame);
        } else if (region.find("poly") != std::string::npos) { // "poly", "polyline"
            region_state = ImportAnnPoly(parameters, coord_frame);
        } else if (region == "global") {
            _global_properties = properties;
        } else {
            _import_errors.append(region + " not supported.\n");
        }

        if (region_state.RegionDefined()) {
            // Set RegionStyle
            auto region_type = region_state.type;
            auto region_style = ImportStyleParameters(region_type, properties);

            if (region_type == CARTA::RegionType::ANNPOINT && parameters.size() == 5) {
                auto symbol_char = parameters[4]; // ann, symbol, x, y, char
                ImportPointStyleParameters(symbol_char, properties, region_style.mutable_annotation_style());
            }

            // Set RegionProperties and add to list
            RegionProperties region_properties(region_state, region_style);
            _import_regions.push_back(region_properties);
        }
    }
}

std::string CrtfImportExport::GetRegionDirectionFrame(std::unordered_map<std::string, std::string>& properties) {
    std::string dir_frame;

    if (properties.count("coord")) {
        dir_frame = properties["coord"];
    } else if (_global_properties.count("coord")) {
        dir_frame = _global_properties["coord"];
    } else {
        dir_frame = GetImageDirectionFrame();
    }

    return dir_frame;
}

RegionState CrtfImportExport::ImportAnnSymbol(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import AnnSymbol to RegionState
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    if (parameters.size() >= 3) { // "(ann) symbol x y" or "text x y", optional symbol shape or text string
        // Convert string to Quantities
        casacore::Quantity x, y;
        try {
            casacore::readQuantity(x, parameters[param_index++]);
            casacore::readQuantity(y, parameters[param_index++]);
        } catch (const casacore::AipsError& err) {
            spdlog::error("{} import Quantity error: {}", region, err.getMesg());
            _import_errors.append(region + " parameters invalid.\n");
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
                CARTA::RegionType type;
                if (region == "symbol") {
                    type = is_annotation ? CARTA::ANNPOINT : CARTA::POINT;
                } else if (region == "text") {
                    type = CARTA::ANNTEXT;
                } else {
                    spdlog::error("Unknown region {} import failed", region);
                    _import_errors.append("Unknown region " + region + " import failed.\n");
                    return region_state;
                }

                float rotation(0.0);
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

RegionState CrtfImportExport::ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import Annotation box in pixel coordinates to RegionState
    RegionState region_state;

    if (parameters.size() >= 5) {
        // [box blcx blcy trcx trcy], [centerbox cx cy width height], or [rotbox cx cy width height angle]
        bool is_annotation = parameters[0] == "ann";
        CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNRECTANGLE : CARTA::RegionType::RECTANGLE);

        // Use parameters to get control points and rotation
        std::vector<CARTA::Point> control_points;
        float rotation(0.0);
        if (!GetBoxControlPoints(parameters, coord_frame, control_points, rotation)) {
            return region_state;
        }

        // Create RegionState and add to vector
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("box syntax invalid.\n");
    }

    return region_state;
}

RegionState CrtfImportExport::ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import AnnEllipse in pixel coordinates to RegionState
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    if (parameters.size() >= 4) {
        // [ellipse cx cy bmaj bmin angle] or [circle cx cy r]
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
                CARTA::RegionType type = (is_annotation ? CARTA::RegionType::ANNELLIPSE : CARTA::RegionType::ELLIPSE);
                if (region == "compass") {
                    type = CARTA::RegionType::ANNCOMPASS;
                }

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

RegionState CrtfImportExport::ImportAnnPoly(std::vector<std::string>& parameters, std::string& coord_frame) {
    // Import polygon, polyline, or line-like regions (line, vector, ruler) in pixel coordinates to RegionState
    RegionState region_state;
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region = parameters[param_index++];

    if (parameters.size() >= 5) {
        // (ann) poly x1 y1 x2 y2 x3 y3 ...
        // (ann) polyline x1 y1 x2 y2 x3 y3...
        // (ann) line x1 y1 x2 y2
        // vector x1 y1 x2 y2
        // ruler x1 y1 x2 y2

        // Check: poly at least 3 points, line etc two points
        if (region.find("poly") == 0) {
            if (parameters.size() < 7) {
                _import_errors.append(region + " syntax invalid.\n");
                return region_state;
            }
        } else if (parameters.size() < 5) {
            _import_errors.append(region + " syntax invalid.\n");
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
                _import_errors.append("Unknown region " + region + " import failed.\n");
                return region_state;
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

CARTA::RegionStyle CrtfImportExport::ImportStyleParameters(
    CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties) {
    // Get CARTA::RegionStyle parameters from properties map
    CARTA::RegionStyle region_style;

    // Common parameters
    // name
    if (properties.count("label")) {
        auto name = properties["label"];
        if (name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.length() - 2);
        }
        region_style.set_name(name);
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
    region_style.set_color(import_color);

    // linewidth
    int line_width(casa::AnnotationBase::DEFAULT_LINEWIDTH);
    if (properties.find("linewidth") != properties.end()) {
        int linewidth;
        if (StringToInt(properties["linewidth"], linewidth)) {
            line_width = linewidth;
        }
    } else if (_global_properties.find("linewidth") != _global_properties.end()) {
        int linewidth;
        if (StringToInt(properties["linewidth"], linewidth)) {
            line_width = linewidth;
        }
    }
    region_style.set_line_width(line_width);

    // linestyle
    std::string linestyle("-"); // solid
    if (properties.count("linestyle")) {
        linestyle = properties["linestyle"];
    } else if (_global_properties.count("linestyle")) {
        linestyle = _global_properties["linestyle"];
    }
    if (linestyle == "-") { // solid line
        region_style.add_dash_list(0);
    } else if (linestyle == ":") { // dotted line
        region_style.add_dash_list(1);
    } else {
        region_style.add_dash_list(REGION_DASH_LENGTH); // CARTA default
    }

    // font
    ImportFontStyleParameters(properties, region_style.mutable_annotation_style());

    return region_style;
}

void CrtfImportExport::ImportFontStyleParameters(
    std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    if (properties.find("font") != properties.end()) {
        annotation_style->set_font(properties["font"]);
    }
    if (properties.find("fontsize") != properties.end()) {
        int fontsize;
        if (StringToInt(properties["fontsize"], fontsize)) {
            annotation_style->set_font_size(fontsize);
        }
    }
    if (properties.find("fontstyle") != properties.end()) {
        auto font_style = properties["fontstyle"];
        if (font_style == "bold-italic") {
            font_style = "bold_italic";
        }
        annotation_style->set_font_style(font_style);
    }
}

void CrtfImportExport::ImportPointStyleParameters(
    const std::string& symbol_char, std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style) {
    // Set point shape from region parameters.
    CARTA::PointAnnotationShape point_shape(CARTA::PointAnnotationShape::SQUARE);
    bool symthick(true);
    if (properties.find("symthick") != properties.end()) {
        symthick = (properties["symthick"] == "1" ? true : false);
    }
    int sym_size(1);
    if (properties.find("symsize") != properties.end()) {
        int symsize;
        if (StringToInt(properties["symsize"], symsize)) {
            sym_size = symsize;
        }
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
    annotation_style->set_point_width(sym_size);
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
    bool is_annotation = parameters[0] == "ann";
    int param_index = is_annotation ? 1 : 0;
    std::string region(parameters[param_index++]);
    casacore::Quantity p1, p2, p3, p4;

    try {
        // Convert parameters to Quantity:
        casacore::readQuantity(p1, parameters[param_index++]);
        casacore::readQuantity(p2, parameters[param_index++]);
        casacore::readQuantity(p3, parameters[param_index++]);
        casacore::readQuantity(p4, parameters[param_index++]);

        if (region == "rotbox") {
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

casa::AnnSymbol::Symbol CrtfImportExport::GetAnnSymbol(CARTA::PointAnnotationShape point_shape) {
    switch (point_shape) {
        case CARTA::PointAnnotationShape::SQUARE:
        case CARTA::PointAnnotationShape::BOX:
            return casa::AnnSymbol::SQUARE;
        case CARTA::PointAnnotationShape::CIRCLE:
        case CARTA::PointAnnotationShape::CIRCLE_LINED:
            return casa::AnnSymbol::CIRCLE;
        case CARTA::PointAnnotationShape::DIAMOND:
        case CARTA::PointAnnotationShape::DIAMOND_LINED:
            return casa::AnnSymbol::DIAMOND;
        case CARTA::PointAnnotationShape::CROSS:
            return casa::AnnSymbol::PLUS;
        case CARTA::PointAnnotationShape::X:
            return casa::AnnSymbol::X;
        default:
            return casa::AnnSymbol::POINT;
    }
}

char CrtfImportExport::GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape) {
    switch (point_shape) {
        case CARTA::PointAnnotationShape::SQUARE:
        case CARTA::PointAnnotationShape::BOX:
            return 's';
        case CARTA::PointAnnotationShape::CIRCLE:
        case CARTA::PointAnnotationShape::CIRCLE_LINED:
            return 'o';
        case CARTA::PointAnnotationShape::DIAMOND:
        case CARTA::PointAnnotationShape::DIAMOND_LINED:
            return 'D';
        case CARTA::PointAnnotationShape::CROSS:
            return '+';
        case CARTA::PointAnnotationShape::X:
            return 'x';
        default:
            return '.';
    }
}

std::string CrtfImportExport::GetRegionColor(const CARTA::RegionStyle& region_style) {
    std::string region_color = region_style.color();
    if (region_color[0] == '#') {
        region_color = region_color.substr(1);
    }
    std::transform(region_color.begin(), region_color.end(), region_color.begin(), ::tolower);
    return region_color;
}

casa::AnnotationBase::LineStyle CrtfImportExport::GetRegionLineStyle(const CARTA::RegionStyle& region_style) {
    casa::AnnotationBase::LineStyle line_style(casa::AnnotationBase::SOLID);
    if ((region_style.dash_list_size() > 0) && (region_style.dash_list(0) != 0)) {
        line_style = casa::AnnotationBase::DASHED;
    }
    return line_style;
}

void CrtfImportExport::GetAnnotationFontParameters(
    const CARTA::RegionStyle& region_style, std::string& font, unsigned int& font_size, casa::AnnotationBase::FontStyle& font_style) {
    font = casa::AnnotationBase::DEFAULT_FONT;
    font_size = casa::AnnotationBase::DEFAULT_FONTSIZE;
    font_style = casa::AnnotationBase::DEFAULT_FONTSTYLE;

    if (region_style.has_annotation_style()) {
        auto region_style_font = region_style.annotation_style().font();
        if (!region_style_font.empty()) {
            font = region_style_font;
        }

        auto region_style_fontsize = region_style.annotation_style().font_size();
        if (region_style_fontsize > 0) {
            font_size = region_style_fontsize;
        }

        std::unordered_map<std::string, casa::AnnotationBase::FontStyle> font_style_map{{"", casa::AnnotationBase::NORMAL},
            {"Normal", casa::AnnotationBase::NORMAL}, {"Bold", casa::AnnotationBase::BOLD}, {"Italic", casa::AnnotationBase::ITALIC},
            {"Italic Bold", casa::AnnotationBase::ITALIC_BOLD}};
        auto region_style_fontstyle = region_style.annotation_style().font_style();
        if (font_style_map.find(region_style_fontstyle) != font_style_map.end()) {
            font_style = font_style_map[region_style_fontstyle];
        }
    }
}

void CrtfImportExport::GetAnnotationSymbolParameters(
    const CARTA::RegionStyle& region_style, unsigned int& symbol_size, unsigned int& symbol_thickness) {
    symbol_size = casa::AnnotationBase::DEFAULT_SYMBOLSIZE;
    symbol_thickness = casa::AnnotationBase::DEFAULT_SYMBOLTHICKNESS;

    if (region_style.has_annotation_style()) {
        auto point_width = region_style.annotation_style().point_width();
        if (point_width > 0) {
            symbol_size = point_width;
        }

        auto point_shape = region_style.annotation_style().point_shape();
        if (point_shape == CARTA::BOX || point_shape == CARTA::CIRCLE_LINED || point_shape == CARTA::DIAMOND_LINED) {
            symbol_thickness = 0;
        }
    }
}

std::string CrtfImportExport::GetAnnotationCoordinateSystem() {
    std::string ann_coord_sys = GetImageDirectionFrame();
    if (ann_coord_sys.empty() && _coord_sys->hasLinearCoordinate()) {
        ann_coord_sys = "linear";
    }
    return ann_coord_sys;
}

void CrtfImportExport::ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    // Add standard CRTF keywords or region type-specific parameters and optional label to region_line
    std::ostringstream oss;

    std::string dir_frame = GetImageDirectionFrame();
    if (!dir_frame.empty()) {
        oss << " coord=" << dir_frame;
    }

    oss << " linewidth=" << region_style.line_width();
    oss << ", linestyle=" << casa::AnnotationBase::lineStyleToString(GetRegionLineStyle(region_style));
    auto region_color = GetRegionColor(region_style);
    oss << ", color=" << region_color;

    // label
    if (!region_style.name().empty()) {
        oss << ", label=\"" << region_style.name() << "\"";
        oss << ", labelcolor=" << region_color;
        oss << ", labelpos=" << casa::AnnotationBase::DEFAULT_LABELPOS;
    }

    // font
    if (!region_style.name().empty() || (region_style.has_annotation_style() && !region_style.annotation_style().font().empty())) {
        std::string font;
        unsigned int font_size;
        casa::AnnotationBase::FontStyle font_style;
        GetAnnotationFontParameters(region_style, font, font_size, font_style);

        // Bug in imageanalysis code: exports as "itatlic_bold" but imports as "bold-italic"
        auto fontstyle_str = casa::AnnotationBase::fontStyleToString(font_style);
        if (fontstyle_str == "itatlic_bold") {
            fontstyle_str = "bold-italic";
        }

        oss << ", font=" << font;
        oss << ", fontsize=" << font_size;
        oss << ", fontstyle=" << fontstyle_str;
        oss << ", usetex=" << (casa::AnnotationBase::DEFAULT_USETEX ? "true" : "false");
    }

    // symbol size, thickness
    if (region_line.find("symbol") != std::string::npos) {
        unsigned int symbol_size, symbol_thickness;
        GetAnnotationSymbolParameters(region_style, symbol_size, symbol_thickness);
        oss << ", symsize=" << symbol_size;
        oss << ", symthick=" << symbol_thickness;
    }
    region_line.append(oss.str());
}

void CrtfImportExport::ExportStyleParameters(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region) {
    // Set region style parameters in AnnotationBase region
    region->setLineWidth(region_style.line_width());
    region->setLineStyle(GetRegionLineStyle(region_style));
    auto region_color = GetRegionColor(region_style);
    region->setColor(region_color);

    // label
    if (!region_style.name().empty()) {
        region->setLabel(region_style.name());
        region->setLabelColor(region_color);
        region->setLabelPosition(casa::AnnotationBase::DEFAULT_LABELPOS);
    }

    // symsize, symthick
    if (region->getType() == casa::AnnotationBase::SYMBOL) {
        unsigned int symbol_size, symbol_thickness;
        GetAnnotationSymbolParameters(region_style, symbol_size, symbol_thickness);

        region->setSymbolSize(symbol_size);
        region->setSymbolThickness(symbol_thickness);
    }

    // font
    if (!region_style.name().empty() || (region_style.has_annotation_style() && !region_style.annotation_style().font().empty())) {
        std::string font;
        unsigned int font_size;
        casa::AnnotationBase::FontStyle font_style;
        GetAnnotationFontParameters(region_style, font, font_size, font_style);

        region->setFont(font);
        region->setFontSize(font_size);
        region->setFontStyle(font_style);
        region->setUseTex(casa::AnnotationBase::DEFAULT_USETEX);
    }
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
