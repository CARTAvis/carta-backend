//# CrtfImportExport.cc: import and export regions in CRTF format

#include "CrtfImportExport.h"

#include <casacore/casa/Quanta/QMath.h>
#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnRectBox.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>
#include <imageanalysis/Annotations/AnnSymbol.h>

#include <carta-protobuf/enums.pb.h>

#include "../Util.h"

using namespace carta;

CrtfImportExport::CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id,
    const std::string& file, bool file_is_filename)
    : RegionImportExport(image_coord_sys, image_shape, file_id) {
    // Use casa RegionTextList to import file (by filename) and create annotation file lines
    if (image_coord_sys.hasDirectionCoordinate()) {
        bool require_region(false); // import regions outside image
        casa::RegionTextList region_list;
        try {
            if (file_is_filename) {
                region_list = casa::RegionTextList(
                    file, image_coord_sys, image_shape, "", "", "", casa::RegionTextParser::CURRENT_VERSION, true, require_region);
            } else {
                region_list = casa::RegionTextList(image_coord_sys, file, image_shape, "", "", "", true, require_region);
            }
        } catch (const casacore::AipsError& err) {
            _import_errors = err.getMesg().before("at File");
            return;
        }

        // Iterate through annotations to create regions
        for (unsigned int iline = 0; iline < region_list.nLines(); ++iline) {
            casa::AsciiAnnotationFileLine file_line = region_list.lineAt(iline);
            try {
                ImportAnnotationFileLine(file_line);
            } catch (const casacore::AipsError& err) {
                _import_errors.append(err.getMesg() + "\n");
            }
        }
    } else {
        _import_errors = "Import error: image coordinate system has no direction coordinate";
    }
}

CrtfImportExport::CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape)
    : RegionImportExport(image_coord_sys, image_shape) {
    // Export regions; will add each region to RegionTextList
    _region_list = casa::RegionTextList(image_coord_sys, image_shape);
}

// Public: for exporting regions

bool CrtfImportExport::AddExportRegion(const RegionState& region_state) {
    // Add pixel region using RegionState
    casa::AnnRegion* ann_region(nullptr);
    casa::AnnSymbol* ann_symbol(nullptr); // not an AnnRegion subclass
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
    bool require_region(false);       // can be outside image
    casa::AnnotationBase::unitInit(); // enable "pix" unit

    try {
        switch (region_state.type) {
            case CARTA::RegionType::POINT: {
                casacore::Quantity x(region_state.control_points[0].x(), "pix");
                casacore::Quantity y(region_state.control_points[0].y(), "pix");
                ann_symbol = new casa::AnnSymbol(x, y, _coord_sys, casa::AnnSymbol::POINT, stokes_types);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                casacore::Quantity cx(region_state.control_points[0].x(), "pix");
                casacore::Quantity cy(region_state.control_points[0].y(), "pix");
                casacore::Quantity xwidth(region_state.control_points[1].x(), "pix");
                casacore::Quantity ywidth(region_state.control_points[1].y(), "pix");
                if (region_state.rotation == 0) {
                    ann_region = new casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    casacore::Quantity angle(region_state.rotation, "deg");
                    ann_region = new casa::AnnRotBox(cx, cy, xwidth, ywidth, angle, _coord_sys, _image_shape, stokes_types, require_region);
                }
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                casacore::Quantity cx(region_state.control_points[0].x(), "pix");
                casacore::Quantity cy(region_state.control_points[0].y(), "pix");
                casacore::Quantity bmaj(region_state.control_points[1].x(), "pix");
                casacore::Quantity bmin(region_state.control_points[1].y(), "pix");
                casacore::Quantity angle(region_state.rotation, "deg");
                ann_region = new casa::AnnEllipse(cx, cy, bmaj, bmin, angle, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                size_t npoints(region_state.control_points.size());
                casacore::Vector<casacore::Quantity> x_coords(npoints), y_coords(npoints);
                for (size_t i = 0; i < npoints; ++i) {
                    x_coords(i) = casacore::Quantity(region_state.control_points[i].x(), "pix");
                    y_coords(i) = casacore::Quantity(region_state.control_points[i].y(), "pix");
                }
                ann_region = new casa::AnnPolygon(x_coords, y_coords, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            default:
                break;
        }

        casacore::CountedPtr<const casa::AnnotationBase> annotation_region;
        if (ann_symbol) {
            if (!region_state.name.empty()) {
                ann_symbol->setLabel(region_state.name);
            }
            annotation_region = casacore::CountedPtr<casa::AnnotationBase>(ann_symbol);
        }
        if (ann_region) {
            ann_region->setAnnotationOnly(false);
            if (!region_state.name.empty()) {
                ann_region->setLabel(region_state.name);
            }
            annotation_region = casacore::CountedPtr<casa::AnnotationBase>(ann_region);
        }

        casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
        _region_list.addLine(file_line);
        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "CRTF export error: " << err.getMesg() << std::endl;
        return false;
    }
}

bool CrtfImportExport::AddExportRegion(const std::string& name, CARTA::RegionType type,
    const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation) {
    // Add world-coord region using input Quantities
    if (control_points.empty()) {
        return false;
    }

    casa::AnnRegion* ann_region(nullptr);
    casa::AnnSymbol* ann_symbol(nullptr); // not an AnnRegion subclass
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
    bool require_region(false); // can be outside image

    try {
        switch (type) {
            case CARTA::RegionType::POINT: {
                casacore::Quantity x(control_points[0]);
                casacore::Quantity y(control_points[1]);
                ann_symbol = new casa::AnnSymbol(x, y, _coord_sys, casa::AnnSymbol::POINT, stokes_types);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                casacore::Quantity cx(control_points[0]);
                casacore::Quantity cy(control_points[1]);
                casacore::Quantity xwidth(control_points[2]);
                casacore::Quantity ywidth(control_points[3]);
                if (rotation.getValue() == 0) {
                    ann_region = new casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    ann_region =
                        new casa::AnnRotBox(cx, cy, xwidth, ywidth, rotation, _coord_sys, _image_shape, stokes_types, require_region);
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
                ann_region = new casa::AnnEllipse(cx, cy, bmaj, bmin, rotangle, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                size_t npoints(control_points.size());
                casacore::Vector<casacore::Quantity> x_coords(npoints / 2), y_coords(npoints / 2);
                int index(0);
                for (size_t i = 0; i < npoints; i += 2) {
                    x_coords(index) = control_points[i];
                    y_coords(index++) = control_points[i + 1];
                }
                ann_region = new casa::AnnPolygon(x_coords, y_coords, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            default:
                break;
        }

        casacore::CountedPtr<const casa::AnnotationBase> annotation_region;
        if (ann_symbol) {
            if (!name.empty()) {
                ann_symbol->setLabel(name);
            }
            annotation_region = casacore::CountedPtr<casa::AnnotationBase>(ann_symbol);
        }
        if (ann_region) {
            ann_region->setAnnotationOnly(false);
            if (!name.empty()) {
                ann_region->setLabel(name);
            }
            annotation_region = casacore::CountedPtr<casa::AnnotationBase>(ann_region);
        }

        casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
        _region_list.addLine(file_line);
    } catch (const casacore::AipsError& err) {
        std::cerr << "CRTF export error: " << err.getMesg() << std::endl;
        return false;
    }

    return true;
}

bool CrtfImportExport::ExportRegions(std::string& filename, std::string& error) {
    // Print regions to CRTF file
    if (_region_list.nLines() == 0) {
        error = "Export region failed: no regions to export.";
        return false;
    }

    std::ofstream export_file(filename);
    try {
        _region_list.print(export_file);
        export_file.close();
    } catch (const casacore::AipsError& err) {
        export_file.close();
        error = err.getMesg();
        return false;
    }
    return true;
}

bool CrtfImportExport::ExportRegions(std::vector<std::string>& contents, std::string& error) {
    // Print regions to CRTF file lines in vector
    if (_region_list.nLines() == 0) {
        error = "Export region failed: no regions to export.";
        return false;
    }

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
    return false;
}

// Process CRTF file import

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
        _import_regions.push_back(region_state);
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
        _import_regions.push_back(region_state);
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
        _import_regions.push_back(region_state);
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
        _import_regions.push_back(region_state);
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

            // Issue 495 check if axes were swapped: print file ellipse parameters
            std::ostringstream ellipse_output;
            ellipse->print(ellipse_output);
            casacore::String outputstr(ellipse_output.str()); // "ellipse [[x, y], [bmaj, bmin], rotang]"
            // create comma-delimited string of quantities
            casacore::String params(outputstr.after("ellipse ")); // remove "ellipse "
            params.gsub("[", "");                                 // remove [
            params.gsub("] ", "],");                              // add comma delimiter
            params.gsub("]", "");                                 // remove ]
            // split string by comma into string vector
            std::vector<std::string> quantities;
            SplitString(params, ',', quantities);
            // check if rotang changed
            casacore::Quantity file_rotang;
            casacore::readQuantity(file_rotang, quantities[4]);
            if (file_rotang != position_angle) {
                // values were swapped, restore file values
                casacore::Quantity file_bmaj, file_bmin;
                casacore::readQuantity(file_bmaj, quantities[2]);
                casacore::readQuantity(file_bmin, quantities[3]);
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
        world_coords.resize(_coord_sys.nPixelAxes(), true);
        _coord_sys.toPixel(pixel_coords, world_coords);
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
            double bmaj_pixel = AngleToPixelLength(bmaj, 0);
            double bmin_pixel = AngleToPixelLength(bmin, 1);
            point.set_x(bmaj_pixel);
            point.set_y(bmin_pixel);
            control_points.push_back(point);
        }

        // Other RegionState params
        std::string name = annotation_region->getLabel();
        CARTA::RegionType type(CARTA::RegionType::ELLIPSE);

        // Create RegionState
        RegionState region_state = RegionState(_file_id, name, type, control_points, rotation);
        _import_regions.push_back(region_state);
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

    // Convert to world axis units
    casacore::Vector<casacore::String> units = _coord_sys.worldAxisUnits();
    angle.convert(units[pixel_axis]);

    casacore::Vector<casacore::Double> increments(_coord_sys.increment());
    return fabs(angle.getValue() / increments[pixel_axis]);
}
