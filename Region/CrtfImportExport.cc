//# CrtfImportExport.cc: import and export regions in CRTF format

#include "CrtfImportExport.h"

#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnRectBox.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>
#include <imageanalysis/Annotations/AnnSymbol.h>
#include <imageanalysis/Annotations/RegionTextList.h>

#include "../Util.h"

using namespace carta;

CrtfImportExport::CrtfImportExport(
    std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Use casa RegionTextList to import file (by filename) and create annotation file lines
    bool require_region(false); // import regions outside image
    casa::RegionTextList region_list(
        filename, image_coord_sys, image_shape, "", "", "", casa::RegionTextParser::CURRENT_VERSION, true, require_region);

    // Iterate through annotations to create regions
    for (unsigned int iline = 0; iline < region_list.nLines(); ++iline) {
        casa::AsciiAnnotationFileLine file_line = region_list.lineAt(iline);
        try {
            ImportAnnotationFileLine(file_line);
        } catch (const casacore::AipsError& err) {
            _import_errors.append(err.getMesg() + "\n");
        }
    }
}

CrtfImportExport::CrtfImportExport(
    const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Use casa RegionTextList to import file (by contents string) and create annotation file lines
    bool require_region(false); // import regions outside image
    casa::RegionTextList region_list(image_coord_sys, contents, image_shape, "", "", "", true, require_region);

    // Iterate through annotations to create regions
    for (unsigned int iline = 0; iline < region_list.nLines(); ++iline) {
        casa::AsciiAnnotationFileLine file_line = region_list.lineAt(iline);
        try {
            ImportAnnotationFileLine(file_line);
        } catch (const casacore::AipsError& err) {
            _import_errors.append(err.getMesg() + "\n");
        }
    }
}

/* TODO: for export
CrtfImportExport::CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord)
    : _coord_sys(image_coord_sys), _pixel_coord(pixel_coord) {
    // set coordinate system
    if (pixel_coord) {
        _direction_ref_frame = "physical";
    } else {
        InitializeDirectionReferenceFrame(); // crtf
        for (auto& coord : _coord_map) {
            if (coord.second == _direction_ref_frame) {
                _direction_ref_frame = coord.first; // convert to ds9
                break;
            }
        }
        if (_direction_ref_frame == "B1950") {
            _direction_ref_frame = "fk4";
        } else if (_direction_ref_frame == "J2000") {
            _direction_ref_frame = "fk5";
        }
    }
}
*/

// Public accessors

std::vector<RegionState> CrtfImportExport::GetImportedRegions(std::string& error) {
    error = _import_errors;
    return _regions;
}

// Process file import

void CrtfImportExport::ImportAnnotationFileLine(casa::AsciiAnnotationFileLine& file_line) {
    // Process a single CRTF annotation file line to set region; adds RegionState to vector
    switch (file_line.getType()) {
        case casa::AsciiAnnotationFileLine::ANNOTATION: {
            auto annotation_base = file_line.getAnnotationBase();
            const casa::AnnotationBase::Type annotation_type = annotation_base->getType();
            casacore::String region_type_str = casa::AnnotationBase::typeToString(annotation_type);

            switch (annotation_type) {
                case casa::AnnotationBase::VECTOR:
                case casa::AnnotationBase::TEXT: {
                    break;
                }
                case casa::AnnotationBase::LINE:
                case casa::AnnotationBase::POLYLINE:
                case casa::AnnotationBase::ANNULUS: {
                    _import_errors += " Region type " + region_type_str + " is not supported yet.\n";
                    break;
                }
                case casa::AnnotationBase::SYMBOL: {
                    ImportAnnSymbol(annotation_base);
                    break;
                }
                case casa::AnnotationBase::RECT_BOX:
                case casa::AnnotationBase::CENTER_BOX: {
                    if (!annotation_base->isAnnotationOnly()) {
                        ImportAnnBox(annotation_base);
                    }
                    break;
                }
                case casa::AnnotationBase::ROTATED_BOX: {
                    if (!annotation_base->isAnnotationOnly()) {
                        ImportAnnRotBox(annotation_base);
                    }
                    break;
                }
                case casa::AnnotationBase::POLYGON: {
                    if (!annotation_base->isAnnotationOnly()) {
                        ImportAnnPolygon(annotation_base);
                    }
                    break;
                }
                case casa::AnnotationBase::CIRCLE:
                case casa::AnnotationBase::ELLIPSE: {
                    if (!annotation_base->isAnnotationOnly()) {
                        ImportAnnEllipse(annotation_base);
                    }
                    break;
                }
            }
            break;
        }
        case casa::AsciiAnnotationFileLine::GLOBAL:
        case casa::AsciiAnnotationFileLine::COMMENT:
        case casa::AsciiAnnotationFileLine::UNKNOWN_TYPE: {
            break;
        }
    }
}

void CrtfImportExport::ImportAnnSymbol(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnSymbol
    const casa::AnnSymbol* point = dynamic_cast<const casa::AnnSymbol*>(annotation_region.get());

    if (point != nullptr) {
        // Get control point as Quantity (world coordinates)
        casacore::MDirection position = point->getDirection();
        casacore::Quantum<casacore::Vector<casacore::Double>> angle = position.getAngle();
        casacore::Vector<casacore::Double> world_coords = angle.getValue();
        world_coords.resize(_coord_sys.nPixelAxes(), true);
        // Convert to pixel coordinates
        casacore::Vector<casacore::Double> pixel_coords;
        _coord_sys.toPixel(pixel_coords, world_coords);

        // Set control points
        std::vector<CARTA::Point> control_points;
        CARTA::Point point;
        point.set_x(pixel_coords[0]);
        point.set_y(pixel_coords[1]);
        control_points.push_back(point);

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::POINT);
        float rotation(0.0);

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _regions.push_back(region_state);
    } else {
        _import_errors.append("symbol region failed.\n");
    }
}

void CrtfImportExport::ImportAnnBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnPolygon (all Annotation boxes are polygons)
    const casa::AnnPolygon* polygon = dynamic_cast<const casa::AnnPolygon*>(annotation_region.get());

    if (polygon != nullptr) {
        // Get polygon pixel verticies (box corners)
        std::vector<casacore::Double> x, y;
        polygon->pixelVertices(x, y);

        // Get control points (cx, cy), (width, height) from corners
        std::vector<CARTA::Point> control_points;
        RectangleControlPointsFromVertices(x, y, control_points);

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::RECTANGLE);
        float rotation(0.0);

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _regions.push_back(region_state);
    } else {
        _import_errors.append("box region failed.\n");
    }
}

void CrtfImportExport::ImportAnnRotBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnRotBox
    // Unfortunately, cannot get original rectangle and rotation from AnnRotBox, it is a polygon
    const casa::AnnRotBox* rotbox = dynamic_cast<const casa::AnnRotBox*>(annotation_region.get());

    if (rotbox != nullptr) {
        // Print region (known format) and parse to get rotbox input params
        std::ostringstream rotbox_output;
        rotbox->print(rotbox_output);
        casacore::String outputstr(rotbox_output.str()); // "rotbox [[x, y], [x_width, y_width], rotang]"

        // Create comma-delimited string of quantities
        casacore::String params(outputstr.after("rotbox ")); // remove rotbox
        params.gsub("[", "");                                // remove [
        params.gsub("] ", "],");                             // add comma delimiter
        params.gsub("]", "");                                // remove ]
        // Split string by comma into string vector
        std::vector<std::string> quantities;
        SplitString(params, ',', quantities);
        // Convert strings to Quantities (Quantum readQuantity)
        casacore::Quantity cx, cy, xwidth, ywidth, rotang;
        casacore::readQuantity(cx, quantities[0]);
        casacore::readQuantity(cy, quantities[1]);
        casacore::readQuantity(xwidth, quantities[2]);
        casacore::readQuantity(ywidth, quantities[3]);
        casacore::readQuantity(rotang, quantities[4]);
        rotang.convert("deg");

        // Make (unrotated) centerbox from parsed quantities then get corners
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
        bool require_region(false);
        casa::AnnCenterBox cbox = casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types, require_region);
        // Get centerbox pixel vertices
        std::vector<casacore::Double> x, y;
        cbox.pixelVertices(x, y);

        // Get control points from corners
        std::vector<CARTA::Point> control_points;
        RectangleControlPointsFromVertices(x, y, control_points);

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::RECTANGLE);
        float rotation = rotang.getValue();

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _regions.push_back(region_state);
    } else {
        _import_errors.append("rotbox region failed.\n");
    }
}

void CrtfImportExport::ImportAnnPolygon(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnPolygon
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

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::POLYGON);
        float rotation(0.0);

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _regions.push_back(region_state);
    } else {
        _import_errors.append("poly region failed.\n");
    }
}

void CrtfImportExport::ImportAnnEllipse(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region) {
    // Create RegionState from casa::AnnEllipse or AnnCircle
    casacore::MDirection center_position;
    casacore::Quantity bmaj, bmin, position_angle;
    float rotation(0.0);
    std::string region_name;

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
        }
    }

    if (have_region_parameters) {
        // set control points
        std::vector<CARTA::Point> control_points;

        // First point: cx, cy in pixel coords
        casacore::Vector<casacore::Double> pixel_coords;
        casacore::Quantum<casacore::Vector<casacore::Double>> angles = center_position.getAngle();
        casacore::Vector<casacore::Double> world_coords = angles.getValue();
        world_coords.resize(_coord_sys.nPixelAxes(), true);
        _coord_sys.toPixel(pixel_coords, world_coords);
        CARTA::Point point;
        point.set_x(pixel_coords[0]);
        point.set_y(pixel_coords[1]);
        control_points.push_back(point);

        // Second point: bmaj, bmin in pixel length
        double bmaj_pixel = AngleToPixelLength(bmaj, 0);
        double bmin_pixel = AngleToPixelLength(bmin, 1);
        point.set_x(bmaj_pixel);
        point.set_y(bmin_pixel);
        control_points.push_back(point);

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::ELLIPSE);

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _regions.push_back(region_state);
    } else {
        _import_errors.append(region_name + " region failed.\n");
    }
}

void CrtfImportExport::RectangleControlPointsFromVertices(
    std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, std::vector<CARTA::Point>& control_points) {
    // Input: pixel vertices x and y
    // Returns: CARTA Points (cx, cy), (width, height)
    // Point 0 is blc, 1 is brc, 2 is trc, 3 is tlc
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
}

casacore::Vector<casacore::Stokes::StokesTypes> CrtfImportExport::GetStokesTypes() {
    // convert ints to stokes types in vector
    casacore::Vector<casacore::Int> istokes;
    if (_coord_sys.hasPolarizationCoordinate()) {
        istokes = _coord_sys.stokesCoordinate().stokes();
    }

    if (istokes.empty()) {
        // make from stokes axis size
        int stokes_axis(_coord_sys.polarizationCoordinateNumber());
        unsigned int nstokes(_image_shape(stokes_axis));
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

double CrtfImportExport::AngleToPixelLength(casacore::Quantity angle, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse radius.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.

    // Find world axis corresponding to input pixel axis
    int coord, world_axis;
    _coord_sys.findWorldAxis(coord, world_axis, pixel_axis);

    // Convert to world axis units
    casacore::Vector<casacore::String> units = _coord_sys.worldAxisUnits();
    angle.convert(units[world_axis]);

    casacore::Vector<casacore::Double> increments(_coord_sys.increment());
    return fabs(angle.getValue() / increments[world_axis]);
}

/*
// For export

void CrtfImportExport::AddRegion(
    const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation) {
    RegionState state(name, type, control_points, rotation);
    _regions.push_back(state);
}

void CrtfImportExport::PrintHeader(std::ostream& os) {
    // print file format, globals, and coord sys
    os << "# Region file format: DS9 CARTA " << VERSION_ID << std::endl;
    Ds9Properties globals;
    os << "global color=" << globals.color << " delete=" << globals.delete_region << " edit=" << globals.edit_region
       << " fixed=" << globals.fixed_region << " font=\"" << globals.font << "\" highlite=" << globals.highlite_region
       << " include=" << globals.include_region << " move=" << globals.move_region << " select=" << globals.select_region << std::endl;
    os << _direction_ref_frame << std::endl;
}

void CrtfImportExport::PrintRegion(unsigned int i, std::ostream& os) {
    // Print Annotation line; ignore comment and global (for now)
    if (i < NumRegions()) {
        auto& region = _regions[i];
        switch (region.type) {
            case CARTA::RegionType::POINT:
                PrintPointRegion(region, os);
                break;
            case CARTA::RegionType::RECTANGLE:
                PrintBoxRegion(region, os);
                break;
            case CARTA::RegionType::ELLIPSE:
                PrintEllipseRegion(region, os);
                break;
            case CARTA::RegionType::POLYGON:
                PrintPolygonRegion(region, os);
                break;
            case CARTA::RegionType::LINE:
            case CARTA::RegionType::POLYLINE:
            case CARTA::RegionType::ANNULUS:
            default:
                break; // not supported yet
        }
        if (!region.name.empty()) {
            os << " # text={" << region.name << "}";
        }
        os << std::endl;
    }
}

void CrtfImportExport::PrintRegionsToFile(std::ofstream& ofs) {
    PrintHeader(ofs);
    for (unsigned int i = 0; i < NumRegions(); ++i) {
        PrintRegion(i, ofs);
    }
}

void CrtfImportExport::PrintBoxRegion(const RegionProperties& properties, std::ostream& os) {
    // box(x,y,width,height,angle)
    std::string ds9_region("box");
    std::vector<casacore::Quantity> points = properties.control_points;
    os << ds9_region << "(";
    if (_file_pixel_coord) {
        os << std::fixed << std::setprecision(2) << points[0].getValue();
        for (size_t i = 1; i < points.size(); ++i) {
            os << "," << points[i].getValue();
        }
        os << "," << std::defaultfloat << std::setprecision(8) << properties.rotation << ")";
    } else {
        casacore::Quantity cx(points[0]), cy(points[1]);
        casacore::Quantity width(points[2]), height(points[3]);
        // adjust width by cosine(declination) for correct export
        if (width.isConform("rad")) {
            width *= cos(cy);
        }
        os << std::fixed << std::setprecision(6) << cx.get("deg").getValue() << ",";
        os << std::fixed << std::setprecision(6) << cy.get("deg").getValue() << ",";

        os << std::fixed << std::setprecision(2) << width.get("arcsec").getValue() << "\""
           << ",";
        os << std::fixed << std::setprecision(2) << height.get("arcsec").getValue() << "\""
           << ",";
        os << std::defaultfloat << std::setprecision(8) << properties.rotation << ")";
    }
}

void CrtfImportExport::PrintEllipseRegion(const RegionProperties& properties, std::ostream& os) {
    // ellipse(x,y,radius,radius,angle) -or- circle(x,y,radius)
    std::vector<casacore::Quantity> points = properties.control_points;
    bool is_circle(points[2].getValue() == points[3].getValue()); // bmaj == bmin
    if (is_circle) {
        os << "circle(";
        if (_file_pixel_coord) {
            os << std::fixed << std::setprecision(2) << points[0].getValue() << "," << points[1].getValue() << "," << points[2].getValue()
               << ")";
        } else {
            os << std::fixed << std::setprecision(6) << points[0].get("deg").getValue() << ",";
            os << std::fixed << std::setprecision(6) << points[1].get("deg").getValue() << ",";
            os << std::fixed << std::setprecision(2) << points[2].get("arcsec").getValue() << "\"";
            os << ")";
        }
    } else {
        os << "ellipse(";
        // angle measured from x-axis
        float angle = properties.rotation + 90.0;
        if (angle > 360.0) {
            angle -= 360.0;
        }
        if (_file_pixel_coord) {
            os << std::fixed << std::setprecision(2) << points[0].getValue();
            for (size_t i = 1; i < points.size(); ++i) {
                os << "," << points[i].getValue();
            }
            os << "," << std::defaultfloat << std::setprecision(8) << angle << ")";
        } else {
            os << std::fixed << std::setprecision(6) << points[0].get("deg").getValue() << ",";
            os << std::fixed << std::setprecision(6) << points[1].get("deg").getValue() << ",";
            os << std::fixed << std::setprecision(2) << points[2].get("arcsec").getValue() << "\""
               << ",";
            os << std::fixed << std::setprecision(2) << points[3].get("arcsec").getValue() << "\""
               << ",";
            os << std::defaultfloat << std::setprecision(8) << angle << ")";
        }
    }
}

void CrtfImportExport::PrintPointRegion(const RegionProperties& properties, std::ostream& os) {
    // point(x,y)
    std::vector<casacore::Quantity> points = properties.control_points;
    os << "point(";
    if (_file_pixel_coord) {
        os << std::fixed << std::setprecision(2) << points[0].getValue() << "," << points[1].getValue() << ")";
    } else {
        os << std::fixed << std::setprecision(6) << points[0].get("deg").getValue() << ",";
        os << std::fixed << std::setprecision(6) << points[1].get("deg").getValue() << ")";
    }
}

void CrtfImportExport::PrintPolygonRegion(const RegionProperties& properties, std::ostream& os) {
    // polygon(x1,y1,x2,y2,x3,y3,...)
    std::vector<casacore::Quantity> points = properties.control_points;
    os << "polygon(";
    if (_file_pixel_coord) {
        os << std::fixed << std::setprecision(2) << points[0].getValue();
        for (size_t i = 1; i < points.size(); ++i) {
            os << "," << points[i].getValue();
        }
        os << ")";
    } else {
        os << std::fixed << std::setprecision(6) << points[0].get("deg").getValue();
        for (size_t i = 1; i < points.size(); ++i) {
            os << "," << std::fixed << std::setprecision(6) << points[i].get("deg").getValue();
        }
        os << ")";
    }
}
*/
