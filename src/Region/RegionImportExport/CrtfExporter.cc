/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfExporter.cc: export regions in CRTF format

#include "CrtfExporter.h"

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

CrtfExporter::CrtfExporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape, int stokes_axis)
    : RegionExporter(coord_sys, shape), _stokes_axis(stokes_axis) {
    _region_names = GetRegionTypeNames(CARTA::FileType::CRTF);
    _file_coord_frame = GetImageDirectionFrame(_coord_sys);
}

bool CrtfExporter::AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) {
    auto region_type = region_state.type;
    std::vector<CARTA::Point> points = region_state.control_points;
    float rotation = region_state.rotation;

    // Add CRTF-formatted file line for region in pixel coordinates
    std::string file_line;
    switch (region_type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            // symbol [[x, y], .]
            std::string symbol(".");
            if (region_style.has_annotation_style()) {
                symbol = GetAnnSymbolCharacter(region_style.annotation_style().point_shape());
            }

            file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], {}]", _region_names[region_type], points[0].x(), points[0].y(), symbol);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNTEXT: {
            std::string region_name;
            if (rotation == 0.0) {
                // centerbox [[x, y], [width, height]] or textbox [[x, y], [width, height]]
                region_name = (region_type == CARTA::RegionType::ANNTEXT ? "# textbox" : _region_names[region_type]);
                file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix]]", region_name, points[0].x(), points[0].y(),
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
                file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", region_name, points[0].x(),
                    points[0].y(), points[1].x(), points[1].y(), rotation);
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE: {
            // ellipse [[x, y], [radius, radius], angle] OR circle[[x, y], r] OR "## compass[[x, y], length]"
            if (points[1].x() == points[1].y()) { // bmaj == bmin
                std::string name = (region_type == CARTA::RegionType::ELLIPSE ? "circle" : "ann circle");
                file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], {:.4f}pix]", name, points[0].x(), points[0].y(), points[1].x());
            } else {
                file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], [{:.4f}pix, {:.4f}pix], {}deg]", _region_names[region_type],
                    points[0].x(), points[0].y(), points[1].x(), points[1].y(), rotation);
            }
            break;
        }
        case CARTA::RegionType::ANNCOMPASS: {
            // # compass [[x, y], length]
            file_line = fmt::format(
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
            file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix]", _region_names[region_type], points[0].x(), points[0].y());
            for (size_t i = 1; i < points.size(); ++i) {
                file_line += fmt::format(", [{:.4f}pix, {:.4f}pix]", points[i].x(), points[i].y());
            }
            file_line += "]";
            break;
        }
        default:
            break;
    }

    // Add style and add line to file lines
    if (!file_line.empty()) {
        switch (region_type) {
            case CARTA::RegionType::ANNRULER: {
                AddStyle(region_style, file_line);
                std::string unit = (_file_coord_frame == "image" || _file_coord_frame == "linear" ? "image" : "degrees");
                file_line += fmt::format(" ruler={} {}", _file_coord_frame, unit);
                break;
            }
            case CARTA::RegionType::ANNCOMPASS: {
                AddStyle(region_style, file_line);
                AddCompassStyle(region_style, _file_coord_frame, file_line);
                break;
            }
            case CARTA::RegionType::ANNTEXT: {
                // Add textbox line
                file_line += fmt::format(
                    " label=\"{}\", align={}", region_style.name(), text_positions[region_style.annotation_style().text_position()]);
                _file_lines.push_back(file_line);

                // Add text line with center point and style parameters
                file_line = fmt::format("{} [[{:.4f}pix, {:.4f}pix], \"{}\"]", _region_names[region_type], points[0].x(), points[0].y(),
                    region_style.annotation_style().text_label0());
                AddStyle(region_style, file_line);
                break;
            }
            default:
                AddStyle(region_style, file_line);
        }

        _file_lines.push_back(file_line);
        return true;
    }

    return false;
}

bool CrtfExporter::AddRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) {
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
        SetAnnotationRegionStyle(region_style, ann_region);
        ann_region->print(oss);
        delete ann_region;
    }
    if (ann_base) {
        SetAnnotationRegionStyle(region_style, ann_base);
        ann_base->print(oss);
        delete ann_base;
    }

    // Adjust region line, add style, and add to file lines
    std::string file_line = oss.str();
    if (region_type == CARTA::RegionType::ANNTEXT) {
        return AddTextFileLines(region_type, file_line, control_points, region_style);
    }
    return AddFileLine(region_type, file_line, region_style);
}

bool CrtfExporter::ExportRegions(const std::string& filename, std::string& error) {
    if (_file_lines.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    // Print header and regions to output filestream
    std::ofstream export_file(filename);
    export_file << GetFileHeader();
    for (auto& region : _file_lines) {
        export_file << region << "\n";
    }
    export_file.close();
    return true;
}

bool CrtfExporter::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    if (_file_lines.empty()) {
        error = "Export region failed: no regions to export.";
        return false;
    }
    // Append header and regions to contents vector
    contents.push_back(GetFileHeader());
    for (auto& region : _file_lines) {
        contents.push_back(region);
    }
    return true;
}

bool CrtfExporter::GetAnnRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
    const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style, casa::AnnotationBase*& ann_base,
    casa::AnnRegion*& ann_region) {
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

bool CrtfExporter::AddFileLine(CARTA::RegionType region_type, std::string& file_line, const CARTA::RegionStyle& region_style) {
    if (file_line.empty()) {
        return false;
    }

    // Adjust font style in region line printed from CASA region
    FixFontstyle(file_line);

    try {
        // Fix region names printed by casa (different name or unsupported region)
        switch (region_type) {
            case CARTA::RegionType::POLYLINE: {
                file_line.insert(4, "line"); // "poly" -> "polyline"
                break;
            }
            case CARTA::RegionType::ANNPOLYLINE: {
                file_line.insert(8, "line"); // "ann poly" -> "ann polyline"
                break;
            }
            case CARTA::RegionType::ANNPOINT:
            case CARTA::RegionType::ANNLINE: {
                file_line = "ann " + file_line; // add explicit "ann"
                break;
            }
            case CARTA::RegionType::ANNRULER: {
                file_line.replace(0, 4, _region_names[region_type]); // "line" -> "# ruler"
                std::string unit = (_file_coord_frame == "image" || _file_coord_frame == "linear" ? "image" : "degrees");
                file_line += fmt::format(" ruler={} {}", _file_coord_frame, unit);
                break;
            }
            case CARTA::RegionType::ANNCOMPASS: {
                file_line.replace(0, 10, _region_names[region_type]); // "ann circle" -> "# compass"
                AddCompassStyle(region_style, _file_coord_frame, file_line);
                break;
            }
            default:
                break; // type supported by casa
        }

        // Add line to export regions list
        _file_lines.push_back(file_line);
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }

    return true;
}

bool CrtfExporter::AddTextFileLines(CARTA::RegionType region_type, std::string& file_line,
    const std::vector<casacore::Quantity>& control_points, const CARTA::RegionStyle& region_style) {
    if (file_line.empty()) {
        return false;
    }

    FixFontstyle(file_line);

    try {
        // Add textbox line (formatted like centerbox/rotbox)
        if (file_line.find("centerbox") != std::string::npos) {
            file_line.replace(0, 13, "# textbox", 9); // "ann centerbox" -> "# textbox"
        } else {
            file_line.replace(0, 10, "# textbox", 9); // "ann rotbox" -> "# textbox"
        }
        file_line += fmt::format(" align={}", text_positions[region_style.annotation_style().text_position()]);
        _file_lines.push_back(file_line);

        // Add text line using AnnText
        std::string text;
        if (region_style.has_annotation_style()) {
            text = region_style.annotation_style().text_label0();
        }
        auto stokes_types = GetStokesTypes();

        casa::AnnotationBase* ann_base = new casa::AnnText(control_points[0], control_points[1], *_coord_sys, text, stokes_types);
        SetAnnotationRegionStyle(region_style, ann_base);
        std::ostringstream oss;
        ann_base->print(oss);
        delete ann_base;

        file_line = oss.str();
        FixFontstyle(file_line);
        _file_lines.push_back(file_line);
    } catch (const casacore::AipsError& err) {
        spdlog::error("CRTF export error for region type {}: {}", region_type, err.getMesg());
        return false;
    }

    return true;
}

void CrtfExporter::FixFontstyle(std::string& file_line) {
    if (file_line.find("itatlic_bold") != std::string::npos) {
        file_line.replace(file_line.find("itatlic_bold"), 12, "bold-italic", 11); // string expected by CASA import
    }
}

casa::AnnSymbol::Symbol CrtfExporter::GetAnnSymbol(CARTA::PointAnnotationShape point_shape) {
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

char CrtfExporter::GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape) {
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

std::string CrtfExporter::FormatColor(const std::string& color) {
    std::string export_color = color;
    if (export_color[0] == '#') {
        export_color = export_color.substr(1);
    }
    std::transform(export_color.begin(), export_color.end(), export_color.begin(), ::tolower);
    return export_color;
}

casa::AnnotationBase::LineStyle CrtfExporter::GetLineStyle(const CARTA::RegionStyle& region_style) {
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

void CrtfExporter::GetFontStyle(
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

void CrtfExporter::GetSymbolStyle(const CARTA::RegionStyle& region_style, unsigned int& symbol_size, unsigned int& symbol_thickness) {
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

void CrtfExporter::AddStyle(const CARTA::RegionStyle& region_style, std::string& file_line) {
    std::ostringstream oss;
    if (!_file_coord_frame.empty()) {
        oss << " coord=" << _file_coord_frame;
    }

    oss << ", linewidth=" << region_style.line_width();
    oss << ", linestyle=" << casa::AnnotationBase::lineStyleToString(GetLineStyle(region_style));
    auto color = FormatColor(region_style.color());
    oss << ", color=" << color;

    // label
    if (!region_style.name().empty()) {
        oss << ", label=\"" << region_style.name() << "\"";
        oss << ", labelcolor=" << color;
        oss << ", labelpos=" << casa::AnnotationBase::DEFAULT_LABELPOS;
    }

    // font
    if (!region_style.name().empty() || (region_style.has_annotation_style() && !region_style.annotation_style().font().empty())) {
        std::string font;
        unsigned int font_size;
        casa::AnnotationBase::FontStyle font_style;
        GetFontStyle(region_style, font, font_size, font_style);

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
    if (file_line.find("symbol") != std::string::npos) {
        unsigned int symbol_size, symbol_thickness;
        GetSymbolStyle(region_style, symbol_size, symbol_thickness);
        oss << ", symsize=" << symbol_size;
        oss << ", symthick=" << symbol_thickness;
    }
    file_line.append(oss.str());
}

void CrtfExporter::SetAnnotationRegionStyle(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region) {
    region->setLineWidth(region_style.line_width());
    region->setLineStyle(GetLineStyle(region_style));
    auto color = FormatColor(region_style.color());
    region->setColor(color);

    // label
    if (!region_style.name().empty()) {
        region->setLabel(region_style.name());
        region->setLabelColor(color);
        region->setLabelPosition(casa::AnnotationBase::DEFAULT_LABELPOS);
    }

    // symsize, symthick
    if (region->getType() == casa::AnnotationBase::SYMBOL) {
        unsigned int symbol_size, symbol_thickness;
        GetSymbolStyle(region_style, symbol_size, symbol_thickness);

        region->setSymbolSize(symbol_size);
        region->setSymbolThickness(symbol_thickness);
    }

    // font
    if (!region_style.name().empty() || (region_style.has_annotation_style() && !region_style.annotation_style().font().empty())) {
        std::string font;
        unsigned int font_size;
        casa::AnnotationBase::FontStyle font_style;
        GetFontStyle(region_style, font, font_size, font_style);

        region->setFont(font);
        region->setFontSize(font_size);
        region->setFontStyle(font_style);
        region->setUseTex(casa::AnnotationBase::DEFAULT_USETEX);
    }
}

casacore::Vector<casacore::Stokes::StokesTypes> CrtfExporter::GetStokesTypes() {
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

std::string CrtfExporter::GetFileHeader() {
    std::ostringstream header;
    // First lines note CRTF-format region file and version
    header << "#CRTFv" << casa::RegionTextParser::CURRENT_VERSION;
    header << " CASA Region Text Format version " << casa::RegionTextParser::CURRENT_VERSION << std::endl;
    return header.str();
}
