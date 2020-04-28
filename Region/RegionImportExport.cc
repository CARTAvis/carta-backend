#include "RegionImportExport.h"

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <imageanalysis/Annotations/AnnotationBase.h>

using namespace carta;

RegionImportExport::RegionImportExport(
    const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Constructor for import. Use GetImportedRegions to retrieve regions.
}

RegionImportExport::RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys), _image_shape(image_shape) {
    // Constructor for export. Use AddExportRegion to add regions, then ExportRegions to finalize
}

// public accessors

std::vector<RegionState> RegionImportExport::GetImportedRegions(std::string& error) {
    // Parse the file in the constructor to create RegionStates with given reference file_id; return any errors in error
    error = _import_errors;

    if ((_import_regions.size() == 0) && error.empty()) {
        error = "Import error: zero regions set.";
    }

    return _import_regions;
}

bool RegionImportExport::AddExportRegion(
    const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord) {
    // Convert Record to Quantities for region type then set region
    // Record is in pixel coords; convert to world coords if needed
    std::vector<casacore::Quantity> control_points;
    casacore::Quantity qrotation(90.0, "deg"); // default for regions with no rotation
    if (pixel_coord) {
        casa::AnnotationBase::unitInit(); // enable "pix" unit
    }

    bool converted(false);
    switch (region_state.type) {
        case CARTA::RegionType::POINT:
            converted = ConvertRecordToPoint(region_record, pixel_coord, control_points);
            break;
        case CARTA::RegionType::RECTANGLE:
            converted = ConvertRecordToRectangle(region_state, region_record, pixel_coord, control_points, qrotation);
            break;
        case CARTA::RegionType::ELLIPSE:
            converted = ConvertRecordToEllipse(region_state, region_record, pixel_coord, control_points, qrotation);
            break;
        case CARTA::RegionType::POLYGON:
            converted = ConvertRecordToPolygon(region_record, pixel_coord, control_points);
            break;
        default:
            break;
    }

    if (converted) {
        return AddExportRegion(region_state.name, region_state.type, control_points, qrotation); // add to CRTF or DS9 export
    }

    return converted;
}

bool RegionImportExport::ConvertRecordToPoint(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to point Quantity control points
    // Point is an LCBox with blc, trc arrays in pixel coordinates (blc = trc)
    casacore::Vector<casacore::Float> blc = region_record.asArrayFloat("blc");

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        blc -= (float)1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(blc(0), "pix"));
        control_points.push_back(casacore::Quantity(blc(1), "pix"));
        return true;
    }

    // For world coords, convert to Double
    casacore::Vector<casacore::Double> pixel_coords(blc.size());
    for (auto i = 0; i < blc.size(); ++i) {
        pixel_coords(i) = blc(i);
    }

    try {
        // Convert pixel to world
        casacore::Vector<casacore::Double> world_coords = _coord_sys.toWorld(pixel_coords);

        // Add Quantities to control_points
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));
        return true;
    } catch (const casacore::AipsError& err) {
        return false;
    }
}

bool RegionImportExport::ConvertRecordToRectangle(const RegionState& region_state, const casacore::RecordInterface& region_record,
    bool pixel_coord, std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation) {
    // Convert casacore Record to box Quantity control points
    // Rectangles are exported to Record as LCPolygon with 4 points: blc, brc, trc, tlc
    if (region_state.rotation != 0.0) {
        return ConvertRecordToRotBox(region_state, region_record, pixel_coord, control_points, qrotation);
    }

    casacore::Vector<casacore::Float> x = region_record.asArrayFloat("x");
    casacore::Vector<casacore::Float> y = region_record.asArrayFloat("y");

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        x -= (float)1.0;
        y -= (float)1.0;
    }

    if (pixel_coord) {
        casacore::Double blc_x = x[0];
        casacore::Double trc_x = x[2];
        casacore::Double blc_y = y[0];
        casacore::Double trc_y = y[2];
        // Control points: center point, width/height
        double cx = (blc_x + trc_x) / 2.0;
        double cy = (blc_y + trc_y) / 2.0;
        double width = fabs(trc_x - blc_x);
        double height = fabs(trc_y - blc_y);

        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(cx, "pix"));
        control_points.push_back(casacore::Quantity(cy, "pix"));
        control_points.push_back(casacore::Quantity(width, "pix"));
        control_points.push_back(casacore::Quantity(height, "pix"));
        return true;
    }

    // For world coords, convert to Double
    size_t npoints(4); // corners
    size_t naxes(_image_shape.size());
    casacore::Vector<casacore::Double> x_pixel(npoints);
    casacore::Vector<casacore::Double> y_pixel(npoints);
    for (auto i = 0; i < npoints; ++i) {
        x_pixel(i) = x(i);
        y_pixel(i) = y(i);
    }

    // Convert corner pixel coords to world coords
    casacore::Matrix<casacore::Double> world_coords(naxes, npoints);
    casacore::Matrix<casacore::Double> pixel_coords(naxes, npoints);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x_pixel;
    pixel_coords.row(1) = y_pixel;
    casacore::Vector<casacore::Bool> failures;
    try {
        if (_coord_sys.toWorldMany(world_coords, pixel_coords, failures)) {
            // Make x and y world coord Vectors
            casacore::Vector<casacore::Double> x_world = world_coords.row(0);
            casacore::Vector<casacore::Double> y_world = world_coords.row(1);

            // Point 0 is blc, point 2 is trc
            casacore::Double blc_x = x_world[0];
            casacore::Double trc_x = x_world[2];
            casacore::Double blc_y = y_world[0];
            casacore::Double trc_y = y_world[2];
            // Control points: center point, width/height
            casacore::Double cx = (blc_x + trc_x) / 2.0;
            casacore::Double cy = (blc_y + trc_y) / 2.0;
            casacore::Double width = fabs(trc_x - blc_x);
            casacore::Double height = fabs(trc_y - blc_y);

            // Convert to Quantities and add to control_points
            casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
            control_points.push_back(casacore::Quantity(cx, world_units(0)));
            control_points.push_back(casacore::Quantity(cy, world_units(1)));
            control_points.push_back(casacore::Quantity(width, world_units(0)));
            control_points.push_back(casacore::Quantity(height, world_units(1)));
            return true;
        } else {
            return false;
        }
    } catch (const casacore::AipsError& err) {
        return false;
    }
}

bool RegionImportExport::ConvertRecordToRotBox(const RegionState& region_state, const casacore::RecordInterface& region_record,
    bool pixel_coord, std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation) {
    // Convert casacore Record to rotated box Quantity control points
    casacore::Vector<casacore::Float> x = region_record.asArrayFloat("x");
    casacore::Vector<casacore::Float> y = region_record.asArrayFloat("y");
    return false;
}

bool RegionImportExport::ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record,
    bool pixel_coord, std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation) {
    // Convert casacore Record to ellipse Quantity control points
    casacore::Vector<casacore::Float> center = region_record.asArrayFloat("center");
    casacore::Vector<casacore::Float> radii = region_record.asArrayFloat("radii");
    casacore::Float theta = region_record.asFloat("theta"); // radians
    qrotation = casacore::Quantity(theta, "rad");
    qrotation.convert("deg"); // CASA rotang, from x-axis

    CARTA::Point ellipse_axes = region_state.control_points[1];
    bool reversed((ellipse_axes.x() < ellipse_axes.y()) == (radii(0) > radii(1)));

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        center -= (float)1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(center(0), "pix"));
        control_points.push_back(casacore::Quantity(center(1), "pix"));
        // Restore original axes order; oddly, rotation angle was not changed
        if (reversed) {
            control_points.push_back(casacore::Quantity(radii(1), "pix"));
            control_points.push_back(casacore::Quantity(radii(0), "pix"));
            qrotation += 90.0;
            if (qrotation.getValue() > 360.0) {
                qrotation -= 360.0;
            }
        } else {
            control_points.push_back(casacore::Quantity(radii(0), "pix"));
            control_points.push_back(casacore::Quantity(radii(1), "pix"));
        }
        return true;
    }

    casacore::Vector<casacore::Double> pixel_coords(_image_shape.size());
    pixel_coords == 0.0;
    pixel_coords(0) = center(0);
    pixel_coords(1) = center(1);

    try {
        // Convert center pixel to world and add to control points
        casacore::Vector<casacore::Double> world_coords = _coord_sys.toWorld(pixel_coords);
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));

        // Convert (lattice region) axes pixel to world and add to control points
        casacore::Quantity bmaj = _coord_sys.toWorldLength(radii(0), 0);
        casacore::Quantity bmin = _coord_sys.toWorldLength(radii(1), 1);
        // Restore original axes order; oddly, rotation angle was not changed
        if (reversed) {
            control_points.push_back(bmin);
            control_points.push_back(bmaj);
            qrotation += 90.0;
            if (qrotation.getValue() > 360.0) {
                qrotation -= 360.0;
            }
        } else {
            control_points.push_back(bmaj);
            control_points.push_back(bmin);
        }
        return true;
    } catch (const casacore::AipsError& err) {
        return false;
    }
    return false;
}

bool RegionImportExport::ConvertRecordToPolygon(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to polygon Quantity control points
    // Polygon is an LCPolygon with x, y arrays in pixel coordinates
    casacore::Vector<casacore::Float> x = region_record.asArrayFloat("x");
    casacore::Vector<casacore::Float> y = region_record.asArrayFloat("y");
    size_t npoints(x.size() - 1); // remove last point, same as the first to enclose region
    size_t naxes(_image_shape.size());

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        x -= (float)1.0;
        y -= (float)1.0;
    }

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        for (auto i = 0; i < npoints; ++i) {
            control_points.push_back(casacore::Quantity(x(i), "pix"));
            control_points.push_back(casacore::Quantity(y(i), "pix"));
        }
        return true;
    }

    // For world coords, convert to Double
    casacore::Vector<casacore::Double> x_pixel(npoints);
    casacore::Vector<casacore::Double> y_pixel(npoints);
    for (auto i = 0; i < npoints; ++i) {
        x_pixel(i) = x(i);
        y_pixel(i) = y(i);
    }

    // Convert pixel coords to world coords
    casacore::Matrix<casacore::Double> world_coords(naxes, npoints);
    casacore::Matrix<casacore::Double> pixel_coords(naxes, npoints);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x_pixel;
    pixel_coords.row(1) = y_pixel;
    casacore::Vector<casacore::Bool> failures;

    try {
        if (_coord_sys.toWorldMany(world_coords, pixel_coords, failures)) {
            // Make x and y world coord Vectors
            casacore::Vector<casacore::Double> x_world = world_coords.row(0);
            casacore::Vector<casacore::Double> y_world = world_coords.row(1);

            // Convert x and y Vectors to Quantities and add to control_points
            casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
            for (auto i = 0; i < npoints; ++i) {
                control_points.push_back(casacore::Quantity(x_world(i), world_units(0)));
                control_points.push_back(casacore::Quantity(y_world(i), world_units(1)));
            }
            return true;
        } else {
            return false;
        }
    } catch (const casacore::AipsError& err) {
        return false;
    }
}
