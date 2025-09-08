/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfExport.cc: export regions in CRTF format

#include "CrtfExport.h"

#include <spdlog/fmt/fmt.h>

#include <casacore/casa/Quanta/QMath.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnLine.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnPolyline.h>
#include <imageanalysis/Annotations/AnnRotBox.h>
#include <imageanalysis/Annotations/AnnText.h>
#include <imageanalysis/Annotations/AnnVector.h>
#include <imageanalysis/IO/RegionTextParser.h>

#include "Logger/Logger.h"

using namespace carta;

CrtfExport::CrtfExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis)
    : RegionExport(image_coord_sys, image_shape), _stokes_axis(stokes_axis) {
    _region_names = GetRegionTypeNames(CARTA::FileType::CRTF);
}

bool CrtfExport::AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
    // Add pixel region using RegionState
    auto region_type = region_state.type;
    std::vector<CARTA::Point> points = region_state.control_points;
    float angle = region_state.rotation;
    std::string region_line;

    // Print region parameters (pixel coordinates) to CRTF-format string
    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // symbol [[x, y], .]
            std::string symbol(".");
            if (region_style.has_annotation_style()) {
                symbol = GetAnnSymbolCharacter(region_style.annotation_style().point_shape());
            }

            region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], {}]", _region_names[region_type], points[0].x(), points[0].y(), symbol);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNTEXT: {
            std::string region_name;
            if (angle == 0.0) {
                // centerbox [[x, y], [width, height]] or textbox [[x, y], [width, height]]
                region_name = (region_type == CARTA::RegionType::ANNTEXT ? "# textbox" : _region_names[region_type]);
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix]]", region_name, points[0].x(), points[0].y(),
                    points[1].x(), points[1].y());
            } else {
                // rotbox [[x, y], [width, height], angle] or textbox with angle
                if (region_type == CARTA::RegionType::RECTANGLE) {
                    region_name = "rotbox";
                } else if (region_type == CARTA::RegionType::ANNRECTANGLE) {
                    region_name = "ann rotbox";
                } else {
                    region_name = "# textbox";
                }
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", region_name, points[0].x(),
                    points[0].y(), points[1].x(), points[1].y(), angle);
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
        switch (region_type) {
            case CARTA::RegionType::ANNRULER: {
                ExportStyleParameters(region_style, region_line);
                auto coord_sys = GetAnnotationCoordinateSystem();
                std::string unit = (coord_sys == "image" || coord_sys == "linear" ? "image" : "degrees");
                region_line += fmt::format(" ruler={} {}", coord_sys, unit);
                break;
            }
            case CARTA::RegionType::ANNCOMPASS: {
                ExportStyleParameters(region_style, region_line);
                ExportAnnCompassStyle(region_style, GetAnnotationCoordinateSystem(), region_line);
                break;
            }
            case CARTA::RegionType::ANNTEXT: {
                // Add textbox line
                region_line += fmt::format(
                    " label=\"{}\", align={}", region_style.name(), text_positions[region_style.annotation_style().text_position()]);
                _export_regions.push_back(region_line);

                // Add text line with center point
                region_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], \"{}\"]", _region_names[region_type], points[0].x(), points[0].y(),
                    region_style.annotation_style().text_label0());
                ExportStyleParameters(region_style, region_line);
                break;
            }
            default:
                ExportStyleParameters(region_style, region_line);
        }

        _export_regions.push_back(region_line);
        return true;
    }

    return false;
}

bool CrtfExport::AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
    // Create casa::AnnotationBase region from control point Quantities to print in export format
    if (control_points.empty()) {
        return false;
    }

    // Create casa region then print it
    casa::AnnotationBase* ann_base(nullptr); // symbol, line, and polyline
    casa::AnnRegion* ann_region(nullptr);    // all other regions
    if (!GetAnnRegion(region_type, control_points, rotation, region_style, ann_base, ann_region)) {
        return false;
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

    // Adjust region line, add style, and add to export regions vector
    std::string region_line = oss.str();
    if (region_type == CARTA::RegionType::ANNTEXT) {
        return AddTextRegionExportLines(region_type, region_line, control_points, region_style);
    }
    return AddRegionExportLine(region_type, region_line, region_style);
}

bool CrtfExport::ExportRegions(const std::string& filename, std::string& error) {
    // Print regions to CRTF file
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    // Print header and regions to output filestream
    std::ofstream export_file(filename);
    export_file << GetCrtfVersionHeader();
    for (auto& region : _export_regions) {
        export_file << region << "\n";
    }
    export_file.close();
    return true;
}

bool CrtfExport::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    // Print regions to CRTF file lines in vector
    if (_export_regions.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }
    // Append header and regions to contents vector
    contents.push_back(GetCrtfVersionHeader());
    for (auto& region : _export_regions) {
        contents.push_back(region);
    }
    return true;
}

bool CrtfExport::GetAnnRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style, casa::AnnotationBase*& ann_base,
    casa::AnnRegion*& ann_region) {
    // Create AnnotationBase or AnnRegion for region type from inputs
    // Common AnnRegion parameters
    auto stokes_types = GetStokesTypes();
    bool require_region(false); // can be outside image

    try {
        switch (region_type) {
            case CARTA::RegionType::POINT:
            case CARTA::RegionType::ANNPOINT: {
                casacore::Quantity x(control_points[0]);
                casacore::Quantity y(control_points[1]);
                casa::AnnSymbol::Symbol symbol(casa::AnnSymbol::POINT);
                if (region_style.has_annotation_style()) {
                    symbol = GetAnnSymbol(region_style.annotation_style().point_shape());
                }
                ann_base = new casa::AnnSymbol(x, y, *_coord_sys, symbol, stokes_types);
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
            case CARTA::RegionType::ANNRECTANGLE:
            case CARTA::RegionType::ANNTEXT: {
                // For text region, export textbox first
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
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }
    return true;
}

bool CrtfExport::AddRegionExportLine(CARTA::RegionType region_type, std::string& region_line, const CARTA::RegionStyle& region_style) {
    // Adjust region line printed by imageanalysis for additional carta region types
    if (region_line.empty()) {
        return false;
    }

    FixFontstyle(region_line);

    try {
        // Set region lines printed by casa to format supported by carta for actual region type
        switch (region_type) {
            case CARTA::RegionType::POLYLINE: {
                region_line.insert(4, "line"); // "poly" -> "polyline"
                break;
            }
            case CARTA::RegionType::ANNPOLYLINE: {
                region_line.insert(8, "line"); // "ann poly" -> "ann polyline"
                break;
            }
            case CARTA::RegionType::ANNPOINT:
            case CARTA::RegionType::ANNLINE: {
                region_line = "ann " + region_line; // add explicit "ann"
                break;
            }
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
                break; // type supported by casa
        }

        // Add line to export regions list
        _export_regions.push_back(region_line);
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }

    return true;
}

bool CrtfExport::AddTextRegionExportLines(CARTA::RegionType region_type, std::string& region_line,
    const std::vector<casacore::Quantity>& control_points, const CARTA::RegionStyle& region_style) {
    // Export textbox line then text line
    if (region_line.empty()) {
        return false;
    }

    FixFontstyle(region_line);

    try {
        // Add textbox line (formatted like centerbox/rotbox)
        if (region_line.find("centerbox") != std::string::npos) {
            region_line.replace(0, 13, "# textbox", 9); // "ann centerbox" -> "# textbox"
        } else {
            region_line.replace(0, 10, "# textbox", 9); // "ann rotbox" -> "# textbox"
        }
        region_line += fmt::format(" align={}", text_positions[region_style.annotation_style().text_position()]);
        _export_regions.push_back(region_line);

        // Add text line using AnnText
        std::string text;
        if (region_style.has_annotation_style()) {
            text = region_style.annotation_style().text_label0();
        }
        auto stokes_types = GetStokesTypes();
        casa::AnnotationBase* ann_base = new casa::AnnText(control_points[0], control_points[1], *_coord_sys, text, stokes_types);
        ExportStyleParameters(region_style, ann_base);
        std::ostringstream oss;
        ann_base->print(oss);
        delete ann_base;

        region_line = oss.str();
        FixFontstyle(region_line);

        // Add line to export regions list
        _export_regions.push_back(region_line);
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }

    return true;
}

void CrtfExport::FixFontstyle(std::string& region_line) {
    // Fix misspelled fontstyle to style imported by imageanalysis
    if (region_line.find("itatlic_bold") != std::string::npos) {
        region_line.replace(region_line.find("itatlic_bold"), 12, "bold-italic", 11);
    }
}

casa::AnnSymbol::Symbol CrtfExport::GetAnnSymbol(CARTA::PointAnnotationShape point_shape) {
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

char CrtfExport::GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape) {
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

std::string CrtfExport::GetRegionColor(const CARTA::RegionStyle& region_style) {
    std::string region_color = region_style.color();
    if (region_color[0] == '#') {
        region_color = region_color.substr(1);
    }
    std::transform(region_color.begin(), region_color.end(), region_color.begin(), ::tolower);
    return region_color;
}

casa::AnnotationBase::LineStyle CrtfExport::GetRegionLineStyle(const CARTA::RegionStyle& region_style) {
    casa::AnnotationBase::LineStyle line_style(casa::AnnotationBase::SOLID);
    if ((region_style.dash_list_size() > 0) && (region_style.dash_list(0) != 0)) {
        if (region_style.dash_list(0) > 1) {
            line_style = casa::AnnotationBase::DASHED;
        } else {
            line_style = casa::AnnotationBase::DOTTED;
        }
    }
    return line_style;
}

void CrtfExport::GetAnnotationFontParameters(
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

void CrtfExport::GetAnnotationSymbolParameters(
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

std::string CrtfExport::GetAnnotationCoordinateSystem() {
    std::string ann_coord_sys = GetImageDirectionFrame(_coord_sys);
    if (ann_coord_sys.empty() && _coord_sys->hasLinearCoordinate()) {
        ann_coord_sys = "linear";
    }
    return ann_coord_sys;
}

void CrtfExport::ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line) {
    // Add standard CRTF keywords or region type-specific parameters and optional label to region_line
    std::ostringstream oss;

    std::string dir_frame = GetImageDirectionFrame(_coord_sys);
    if (!dir_frame.empty()) {
        oss << " coord=" << dir_frame;
    }

    oss << ", linewidth=" << region_style.line_width();
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

void CrtfExport::ExportStyleParameters(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region) {
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

casacore::Vector<casacore::Stokes::StokesTypes> CrtfExport::GetStokesTypes() {
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

std::string CrtfExport::GetCrtfVersionHeader() {
    // First line indicates CRTF region file and version
    std::ostringstream header;
    header << "#CRTFv" << casa::RegionTextParser::CURRENT_VERSION;
    header << " CASA Region Text Format version " << casa::RegionTextParser::CURRENT_VERSION << std::endl;
    return header.str();
}
