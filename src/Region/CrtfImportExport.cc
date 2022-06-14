/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

    bool require_region(false); // import regions outside image

    try {
        if (image_coord_sys->hasDirectionCoordinate()) {
            // Use casa RegionTextList to import file and create annotation file lines
            casa::RegionTextList region_list;
            if (file_is_filename) {
                region_list = casa::RegionTextList(
                    file, *image_coord_sys, image_shape, "", "", "", casa::RegionTextParser::CURRENT_VERSION, true, require_region);
            } else {
                region_list = casa::RegionTextList(*image_coord_sys, file, image_shape, "", "", "", true, require_region);
            }

            // Iterate through annotations to create regions
            for (unsigned int iline = 0; iline < region_list.nLines(); ++iline) {
                casa::AsciiAnnotationFileLine file_line = region_list.lineAt(iline);
                try {
                    ImportAnnotationFileLine(file_line);
                } catch (const casacore::AipsError& err) {
                    // Catch error for this region and continue through list
                    _import_errors.append(err.getMesg() + "\n");
                }
            }
        } else {
            // Workaround for images with no DirectionCoordinate (only if file in pixel coordinates)
            std::vector<std::string> file_lines = ReadRegionFile(file, file_is_filename);
            ProcessFileLines(file_lines);
        }
    } catch (const casacore::AipsError& err) {
        casacore::String errmesg(err.getMesg());
        if (errmesg.contains("pixels are not square") || errmesg.contains("polyline")) {
            // Error thrown by AnnPolygon when importing rotated pixel region in image with non-square pixels, or
            // "polyline" not supported by imageanalysis
            // Try to read file manually:
            try {
                std::vector<std::string> file_lines = ReadRegionFile(file, file_is_filename);
                ProcessFileLines(file_lines);
            } catch (const casacore::AipsError& err) {
                casacore::String error = err.getMesg().before("at File");
                error = error.before("thrown by");
                _import_errors = error;
            }
        } else {
            // Note exception and quit.
            casacore::String error = err.getMesg().before("at File");
            error = error.before("thrown by");
            _import_errors = error;
        }
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

// Process imageanalysis file import
void CrtfImportExport::ImportAnnotationFileLine(casa::AsciiAnnotationFileLine& file_line) {
    // Process a single CRTF annotation file line to set region; adds RegionProperties to vector
    switch (file_line.getType()) {
        case casa::AsciiAnnotationFileLine::ANNOTATION: {
            auto annotation_base = file_line.getAnnotationBase();
            const casa::AnnotationBase::Type annotation_type = annotation_base->getType();
            RegionState region_state;

            switch (annotation_type) {
                case casa::AnnotationBase::VECTOR:
                case casa::AnnotationBase::TEXT:
                case casa::AnnotationBase::ANNULUS: {
                    casacore::String region_type_str = casa::AnnotationBase::typeToString(annotation_type);
                    _import_errors += " Region type " + region_type_str + " not supported.\n";
                    break;
                }
                case casa::AnnotationBase::LINE: {
                    region_state = ImportAnnLine(annotation_base);
                    break;
                }
                case casa::AnnotationBase::POLYLINE: {
                    region_state = ImportAnnPolyline(annotation_base);
                    break;
                }
                case casa::AnnotationBase::SYMBOL: {
                    region_state = ImportAnnSymbol(annotation_base);
                    break;
                }
                case casa::AnnotationBase::RECT_BOX:
                case casa::AnnotationBase::CENTER_BOX: {
                    region_state = ImportAnnBox(annotation_base);
                    break;
                }
                case casa::AnnotationBase::ROTATED_BOX: {
                    region_state = ImportAnnRotBox(annotation_base);
                    break;
                }
                case casa::AnnotationBase::POLYGON: {
                    region_state = ImportAnnPolygon(annotation_base);
                    break;
                }
                case casa::AnnotationBase::CIRCLE:
                case casa::AnnotationBase::ELLIPSE: {
                    region_state = ImportAnnEllipse(annotation_base);
                    break;
                }
            }

            if (region_state.RegionDefined()) {
                // Set RegionStyle
                RegionStyle region_style = ImportStyleParameters(annotation_base);

                // Add RegionProperties to list
                RegionProperties region_properties(region_state, region_style);
                _import_regions.push_back(region_properties);
            }
            break;
        }
        case casa::AsciiAnnotationFileLine::GLOBAL: {
            _global_properties = file_line.getGloabalParams();
            break;
        }
        case casa::AsciiAnnotationFileLine::COMMENT:
        case casa::AsciiAnnotationFileLine::UNKNOWN_TYPE: {
            break;
        }
    }
}

RegionState CrtfImportExport::ImportAnnSymbol(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnSymbol
    RegionState region_state;
    const casa::AnnSymbol* point = dynamic_cast<const casa::AnnSymbol*>(annotation_region.get());

    if (point != nullptr) {
        // Get control point as Quantity (world coordinates)
        casacore::MDirection position = point->getDirection();
        casacore::Quantum<casacore::Vector<casacore::Double>> angle = position.getAngle();
        casacore::Vector<casacore::Double> world_coords = angle.getValue();
        world_coords.resize(_coord_sys->nPixelAxes(), true);
        // Convert to pixel coordinates
        casacore::Vector<casacore::Double> pixel_coords;
        _coord_sys->toPixel(pixel_coords, world_coords);

        // Set control points
        std::vector<CARTA::Point> control_points;
        CARTA::Point point;
        point.set_x(pixel_coords[0]);
        point.set_y(pixel_coords[1]);
        control_points.push_back(point);

        // Set other RegionState parameters
        CARTA::RegionType type(CARTA::RegionType::POINT);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("symbol import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnLine(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnLine
    RegionState region_state;
    const casa::AnnLine* line = dynamic_cast<const casa::AnnLine*>(annotation_region.get());

    if (line != nullptr) {
        // Get end points as Quantity (world coordinates)
        casacore::Vector<casacore::MDirection> endpoints = line->getEndPoints();
        // Set control points
        std::vector<CARTA::Point> control_points;
        for (auto& endpoint : endpoints) {
            casacore::Quantum<casacore::Vector<casacore::Double>> angle = endpoint.getAngle();
            casacore::Vector<casacore::Double> world_coords = angle.getValue();
            world_coords.resize(_coord_sys->nPixelAxes(), true);
            // Convert to pixel coordinates
            casacore::Vector<casacore::Double> pixel_coords;
            _coord_sys->toPixel(pixel_coords, world_coords);

            CARTA::Point point;
            point.set_x(pixel_coords[0]);
            point.set_y(pixel_coords[1]);
            control_points.push_back(point);
        }

        // Set other RegionState parameters
        CARTA::RegionType type(CARTA::RegionType::LINE);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("line import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnPolyline(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnPolyline
    RegionState region_state;
    const casa::AnnPolyline* polyline = dynamic_cast<const casa::AnnPolyline*>(annotation_region.get());

    if (polyline != nullptr) {
        std::vector<casacore::Double> x, y;
        polyline->pixelVertices(x, y);

        // Set control points
        std::vector<CARTA::Point> control_points;
        for (size_t i = 0; i < x.size(); ++i) {
            CARTA::Point point;
            point.set_x(x[i]);
            point.set_y(y[i]);
            control_points.push_back(point);
        }

        // Set other RegionState parameters
        CARTA::RegionType type(CARTA::RegionType::POLYLINE);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("polyline import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnCenterBox or casa::AnnRectBox
    RegionState region_state;

    // All Annotation boxes are polygons.
    const casa::AnnPolygon* polygon = dynamic_cast<const casa::AnnPolygon*>(annotation_region.get());

    if (polygon != nullptr) {
        // Get polygon pixel vertices (box corners)
        std::vector<casacore::Double> x, y;
        polygon->pixelVertices(x, y);

        // Get control points (cx, cy), (width, height) from corners
        // if pixel vertices form rectangle i.e. [[x1, y1], [x2, y1], [x2, y2], [x1, y2]]
        std::vector<CARTA::Point> control_points;
        if (!RectangleControlPointsFromVertices(x, y, control_points)) {
            // Corners do not form rectangle.
            // Print box (known format) and parse to get input params
            std::ostringstream oss;
            polygon->print(oss);
            std::string box_input(oss.str());
            float rotation;
            if (!GetBoxControlPoints(box_input, control_points, rotation)) {
                _import_errors.append("box import failed.");
                return region_state;
            }
        }

        // Add other RegionState params
        CARTA::RegionType type(CARTA::RegionType::RECTANGLE);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("box import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnRotBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnRotBox
    RegionState region_state;

    const casa::AnnRotBox* rotbox = dynamic_cast<const casa::AnnRotBox*>(annotation_region.get());

    if (rotbox != nullptr) {
        // Cannot get center, width, height, and rotation from AnnRotBox, it is a polygon.
        // Print rotbox (known format) and parse to get input params
        std::ostringstream oss;
        rotbox->print(oss);
        std::string rotbox_input(oss.str());

        std::vector<CARTA::Point> control_points;
        float rotation;
        if (!GetBoxControlPoints(rotbox_input, control_points, rotation)) {
            _import_errors.append("rotbox import failed.");
            return region_state;
        }

        // Other RegionState parameters
        CARTA::RegionType type(CARTA::RegionType::RECTANGLE);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("rotbox import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnPolygon(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnPolygon
    RegionState region_state;
    const casa::AnnPolygon* polygon = dynamic_cast<const casa::AnnPolygon*>(annotation_region.get());

    if (polygon != nullptr) {
        // Get polygon pixel verticies (box corners)
        std::vector<casacore::Double> x, y;
        polygon->pixelVertices(x, y);

        // Set control points
        std::vector<CARTA::Point> control_points;
        for (size_t i = 0; i < x.size(); ++i) {
            CARTA::Point point;
            point.set_x(x[i]);
            point.set_y(y[i]);
            control_points.push_back(point);
        }

        // Set RegionState
        CARTA::RegionType type(CARTA::RegionType::POLYGON);
        float rotation(0.0);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append("poly import failed.\n");
    }
    return region_state;
}

RegionState CrtfImportExport::ImportAnnEllipse(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnEllipse or AnnCircle
    RegionState region_state;

    casacore::MDirection center_position;
    casacore::Quantity bmaj, bmin, position_angle;
    float rotation(0.0);
    std::string region_name; // for error message

    bool have_region_parameters(false);
    casa::AnnotationBase::Type ann_type = annotation_region->getType();
    if (ann_type == casa::AnnotationBase::CIRCLE) {
        region_name = "circle";
        const casa::AnnCircle* circle = dynamic_cast<const casa::AnnCircle*>(annotation_region.get());
        if (circle != nullptr) {
            center_position = circle->getCenter();
            bmaj = circle->getRadius();
            bmin = bmaj;
            have_region_parameters = true;
        }
    } else {
        region_name = "ellipse";
        const casa::AnnEllipse* ellipse = dynamic_cast<const casa::AnnEllipse*>(annotation_region.get());
        if (ellipse != nullptr) {
            center_position = ellipse->getCenter();
            bmaj = ellipse->getSemiMajorAxis();
            bmin = ellipse->getSemiMinorAxis();
            position_angle = ellipse->getPositionAngle();
            position_angle.convert("deg");
            rotation = position_angle.getValue();
            have_region_parameters = true;

            // Issue 495 check if axes were swapped: parse input ellipse parameters
            std::ostringstream ellipse_output;
            ellipse->print(ellipse_output);
            std::string outputstr(ellipse_output.str()); // "ellipse [[x, y], [bmaj, bmin], rotang]"
            std::vector<std::string> parameters;
            std::unordered_map<std::string, std::string> properties;
            ParseRegionParameters(outputstr, parameters, properties);

            // Check if rotang changed from region definition when axes were swapped
            casacore::Quantity file_rotang;
            casacore::readQuantity(file_rotang, parameters[5]);
            if (file_rotang != position_angle) {
                // Restore region file values
                casacore::Quantity file_bmaj, file_bmin;
                casacore::readQuantity(file_bmaj, parameters[3]);
                casacore::readQuantity(file_bmin, parameters[4]);
                bmaj = file_bmaj;
                bmin = file_bmin;
                rotation = file_rotang.getValue();
            }
        }
    }

    if (have_region_parameters) {
        // set control points
        std::vector<CARTA::Point> control_points;

        // First point: cx, cy in pixel coords
        casacore::Vector<casacore::Double> pixel_coords;
        casacore::Quantum<casacore::Vector<casacore::Double>> angles = center_position.getAngle();
        casacore::Vector<casacore::Double> world_coords = angles.getValue();
        world_coords.resize(_coord_sys->nPixelAxes(), true);
        _coord_sys->toPixel(pixel_coords, world_coords);
        CARTA::Point point;
        point.set_x(pixel_coords[0]);
        point.set_y(pixel_coords[1]);
        control_points.push_back(point);

        // Second point: bmaj, bmin in pixel length
        if (bmaj.getUnit() == "pix") {
            point.set_x(bmaj.getValue());
            point.set_y(bmin.getValue());
            control_points.push_back(point);
        } else {
            double bmaj_pixel = WorldToPixelLength(bmaj, 0);
            double bmin_pixel = WorldToPixelLength(bmin, 1);
            point.set_x(bmaj_pixel);
            point.set_y(bmin_pixel);
            control_points.push_back(point);
        }

        // Other RegionState parameters
        CARTA::RegionType type(CARTA::RegionType::ELLIPSE);
        region_state = RegionState(_file_id, type, control_points, rotation);
    } else {
        _import_errors.append(region_name + " import failed.\n");
    }
    return region_state;
}

RegionStyle CrtfImportExport::ImportStyleParameters(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    RegionStyle style;
    style.name = annotation_region->getLabel();
    style.color = FormatColor(annotation_region->getColorString());
    style.line_width = annotation_region->getLineWidth();
    if (annotation_region->getLineStyle() == casa::AnnotationBase::SOLID) {
        style.dash_list = {0, 0};
    } else {
        style.dash_list = {REGION_DASH_LENGTH, REGION_DASH_LENGTH};
    }
    return style;
}

void CrtfImportExport::ImportGlobalParameters(std::unordered_map<std::string, std::string>& properties) {
    // Import properties from global file line to map
    // AnnotationBase has keywordToString but not the reverse
    std::unordered_map<std::string, casa::AnnotationBase::Keyword> global_map = {{"coord", casa::AnnotationBase::COORD},
        {"range", casa::AnnotationBase::RANGE}, {"frame", casa::AnnotationBase::FRAME}, {"corr", casa::AnnotationBase::CORR},
        {"veltype", casa::AnnotationBase::VELTYPE}, {"restfreq", casa::AnnotationBase::RESTFREQ},
        {"linewidth", casa::AnnotationBase::LINEWIDTH}, {"linestyle", casa::AnnotationBase::LINESTYLE},
        {"symsize", casa::AnnotationBase::SYMSIZE}, {"symthick", casa::AnnotationBase::SYMTHICK}, {"color", casa::AnnotationBase::COLOR},
        {"font", casa::AnnotationBase::FONT}, {"fontsize", casa::AnnotationBase::FONTSIZE}, {"fontstyle", casa::AnnotationBase::FONTSTYLE},
        {"usetex", casa::AnnotationBase::USETEX}, {"label", casa::AnnotationBase::LABEL}, {"labelcolor", casa::AnnotationBase::LABELCOLOR},
        {"labelpos", casa::AnnotationBase::LABELPOS}, {"labeloff", casa::AnnotationBase::LABELOFF}};

    for (auto& property : properties) {
        if (global_map.count(property.first)) {
            _global_properties[global_map[property.first]] = property.second;
        }
    }
}

// Manual file import

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
        casacore::String coord_frame = GetRegionDirectionFrame(properties);

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

casacore::String CrtfImportExport::GetRegionDirectionFrame(std::unordered_map<std::string, std::string>& properties) {
    casacore::String dir_frame;

    if (properties.count("coord")) {
        dir_frame = properties["coord"];
    } else if (_global_properties.count(casa::AnnotationBase::COORD)) {
        dir_frame = _global_properties[casa::AnnotationBase::COORD];
    } else if (_coord_sys->hasDirectionCoordinate()) {
        casacore::MDirection::Types mdir_type = _coord_sys->directionCoordinate().directionType();
        dir_frame = casacore::MDirection::showType(mdir_type);
    }

    return dir_frame;
}

RegionState CrtfImportExport::ImportAnnSymbol(std::vector<std::string>& parameters, casacore::String& coord_frame) {
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
                CARTA::Point point;
                point.set_x(pixel_coords[0]);
                point.set_y(pixel_coords[1]);
                control_points.push_back(point);

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

RegionState CrtfImportExport::ImportAnnBox(std::vector<std::string>& parameters, casacore::String& coord_frame) {
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

RegionState CrtfImportExport::ImportAnnEllipse(std::vector<std::string>& parameters, casacore::String& coord_frame) {
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
                CARTA::Point point;
                point.set_x(pixel_coords[0]);
                point.set_y(pixel_coords[1]);
                control_points.push_back(point);

                // Set bmaj, bmin or radius
                if (region == "ellipse") {
                    point.set_x(WorldToPixelLength(p3, 0));
                    point.set_y(WorldToPixelLength(p4, 1));
                } else {
                    double radius = WorldToPixelLength(p3, 0);
                    point.set_x(radius);
                    point.set_y(radius);
                }
                control_points.push_back(point);

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

RegionState CrtfImportExport::ImportAnnPolygonLine(std::vector<std::string>& parameters, casacore::String& coord_frame) {
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
                    CARTA::Point point;
                    point.set_x(pixel_coords[0]);
                    point.set_y(pixel_coords[1]);
                    control_points.push_back(point);
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
    } else if (_global_properties.count(casa::AnnotationBase::COLOR)) {
        import_color = FormatColor(_global_properties[casa::AnnotationBase::COLOR]);
    }
    if (std::strtoul(import_color.c_str(), nullptr, 16)) {
        // add prefix if hex
        import_color = "#" + import_color;
    }
    style.color = import_color;

    // linewidth
    if (properties.count("linewidth")) {
        style.line_width = std::stoi(properties["linewidth"]);
    } else if (_global_properties.count(casa::AnnotationBase::LINEWIDTH)) {
        style.line_width = std::stoi(_global_properties[casa::AnnotationBase::LINEWIDTH]);
    } else {
        style.line_width = 1; // CRTF default
    }

    // linestyle
    std::string linestyle("-"); // solid
    if (properties.count("linestyle")) {
        linestyle = properties["linestyle"];
    } else if (_global_properties.count(casa::AnnotationBase::LINESTYLE)) {
        linestyle = _global_properties[casa::AnnotationBase::LINESTYLE];
    }
    if (linestyle == "-") { // solid line
        style.dash_list = {0, 0};
    } else {
        style.dash_list = {REGION_DASH_LENGTH, REGION_DASH_LENGTH};
    }

    return style;
}

// Private import helpers for rectangles

bool CrtfImportExport::RectangleControlPointsFromVertices(
    std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, std::vector<CARTA::Point>& control_points) {
    // Used when imageanalysis has converted box definition to polygon.
    // Input: pixel vertices x and y.  Point 0 is blc, 1 is brc, 2 is trc, 3 is tlc
    // Returns:
    // - control points: (cx, cy), (width, height)
    // - bool: true = vertices form rectangle, false = vertices form polygon

    // Check if vertices form rectangle not polygon
    if ((x[0] != x[3]) || (x[1] != x[2]) || (y[0] != y[1]) || (y[2] != y[3])) {
        return false;
    }

    casacore::Double blc_x = x[0];
    casacore::Double trc_x = x[2];
    casacore::Double blc_y = y[0];
    casacore::Double trc_y = y[2];

    double cx = (blc_x + trc_x) / 2.0;
    double cy = (blc_y + trc_y) / 2.0;
    double width = fabs(trc_x - blc_x);
    double height = fabs(trc_y - blc_y);

    CARTA::Point point;
    point.set_x(cx);
    point.set_y(cy);
    control_points.push_back(point);
    point.set_x(width);
    point.set_y(height);
    control_points.push_back(point);
    return true;
}

bool CrtfImportExport::GetBoxControlPoints(std::string& box_definition, std::vector<CARTA::Point>& control_points, float& rotation) {
    // Parse box definition to get parameters, then get CARTA rectangle control points
    std::vector<std::string> parameters;
    std::unordered_map<std::string, std::string> properties;
    ParseRegionParameters(box_definition, parameters, properties);

    std::string coord_frame = GetRegionDirectionFrame(properties);

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
            // Get width/height in pixel length
            double width_pix = WorldToPixelLength(width, 0);
            double height_pix = WorldToPixelLength(height, 1);

            // Set control points
            CARTA::Point point;
            point.set_x(pixel_coords[0]);
            point.set_y(pixel_coords[1]);
            control_points.push_back(point);
            point.set_x(width_pix);
            point.set_y(height_pix);
            control_points.push_back(point);
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
