#include "RegionImportExport.h"

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>
#include <imageanalysis/Annotations/AnnotationBase.h>

using namespace carta;

RegionImportExport::RegionImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Constructor for import. Use GetImportedRegions to retrieve regions.
}

RegionImportExport::RegionImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape)
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
    if (pixel_coord) {
        casa::AnnotationBase::unitInit(); // enable "pix" unit
    }

    bool converted(false);
    // Return control points and rotation as Quantity; rotation updated for ellipse only
    std::vector<casacore::Quantity> control_points;
    casacore::Quantity rotation(region_state.rotation, "deg");
    switch (region_state.type) {
        case CARTA::RegionType::POINT:
            converted = ConvertRecordToPoint(region_record, pixel_coord, control_points);
            break;
        case CARTA::RegionType::RECTANGLE:
            converted = ConvertRecordToRectangle(region_record, pixel_coord, control_points);
            break;
        case CARTA::RegionType::ELLIPSE:
            converted = ConvertRecordToEllipse(region_state, region_record, pixel_coord, control_points, rotation);
            break;
        case CARTA::RegionType::POLYGON:
            converted = ConvertRecordToPolygon(region_record, pixel_coord, control_points);
            break;
        default:
            break;
    }

    if (converted) {
        return AddExportRegion(region_state.name, region_state.type, control_points, rotation); // add to CRTF or DS9 export
    }

    return converted;
}

bool RegionImportExport::ConvertPointToPixels(
    std::string& region_frame, std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords) {
    if (point.size() != 2) {
        return false;
    }

    // must have matched coordinates
    bool x_is_pix = point[0].getUnit() == "pixel";
    bool y_is_pix = point[1].getUnit() == "pixel";
    if (x_is_pix != y_is_pix) {
        return false;
    }

    // if unit is pixels, just get values
    if (x_is_pix) {
        pixel_coords.resize(2);
        pixel_coords(0) = point[0].getValue();
        pixel_coords(1) = point[1].getValue();
        return true;
    }

    if (_coord_sys->hasDirectionCoordinate()) {
        casacore::MDirection::Types image_direction_type = _coord_sys->directionCoordinate().directionType();

        casacore::MDirection::Types region_direction_type;
        if (region_frame.empty()) {
            region_direction_type = image_direction_type;
        } else {
            casacore::MDirection::getType(region_direction_type, region_frame);
        }

        // Make MDirection from wcs parameter
        casacore::MDirection direction(point[0], point[1], region_direction_type);

        // Convert to image direction
        if (region_direction_type != image_direction_type) {
            try {
                direction = casacore::MDirection::Convert(direction, image_direction_type)();
            } catch (casacore::AipsError& err) {
                _import_errors.append("Conversion of region parameters to image coordinate system failed.\n");
                return false;
            }
        }

        // Convert world to pixel coordinates
        return _coord_sys->directionCoordinate().toPixel(pixel_coords, direction);
    }

    return false;
}

double RegionImportExport::WorldToPixelLength(casacore::Quantity world_length, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse radius or box width.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.

    // Convert to world axis units
    casacore::Vector<casacore::String> units = _coord_sys->worldAxisUnits();
    world_length.convert(units[pixel_axis]);

    // Find pixel length
    casacore::Vector<casacore::Double> increments(_coord_sys->increment());
    return fabs(world_length.getValue() / increments[pixel_axis]);
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
        casacore::Vector<casacore::Double> world_coords = _coord_sys->toWorld(pixel_coords);

        // Add Quantities to control_points
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));
        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Export error: point Record conversion failed:" << err.getMesg() << std::endl;
        return false;
    }
}

bool RegionImportExport::ConvertRecordToRectangle(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to box Quantity control points.
    // Rectangles are exported to Record as LCPolygon with 4 points: blc, brc, trc, tlc.
    // The input Record for a rotbox must be the corners of an unrotated box (rotation in the region state)
    casacore::Vector<casacore::Float> x = region_record.asArrayFloat("x");
    casacore::Vector<casacore::Float> y = region_record.asArrayFloat("y");

    // Make zero-based
    if (region_record.asBool("oneRel")) {
        x -= (float)1.0;
        y -= (float)1.0;
    }

    double cx, cy, width, height;
    casacore::Double blc_x = x[0];
    casacore::Double trc_x = x[2];
    casacore::Double blc_y = y[0];
    casacore::Double trc_y = y[2];
    // Control points: center point, width/height
    cx = (blc_x + trc_x) / 2.0;
    cy = (blc_y + trc_y) / 2.0;
    width = fabs(trc_x - blc_x);
    height = fabs(trc_y - blc_y);

    if (pixel_coord) {
        // Convert pixel value to Quantity in control points
        control_points.push_back(casacore::Quantity(cx, "pix"));
        control_points.push_back(casacore::Quantity(cy, "pix"));
        control_points.push_back(casacore::Quantity(width, "pix"));
        control_points.push_back(casacore::Quantity(height, "pix"));
        return true;
    }

    try {
        // Convert center position to world coords
        casacore::Vector<casacore::Double> world_center;
        casacore::IPosition pixel_center(_coord_sys->nPixelAxes(), 0);
        pixel_center(0) = cx;
        pixel_center(1) = cy;
        _coord_sys->toWorld(world_center, pixel_center);

        // Convert width/height to world coords
        casacore::Quantity world_width = _coord_sys->toWorldLength(width, 0);
        casacore::Quantity world_height = _coord_sys->toWorldLength(height, 1);

        // Convert to Quantities and add to control_points
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_center(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_center(1), world_units(1)));
        control_points.push_back(world_width);
        control_points.push_back(world_height);
        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Export error: rectangle Record conversion failed:" << err.getMesg() << std::endl;
        return false;
    }
}

bool RegionImportExport::ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record,
    bool pixel_coord, std::vector<casacore::Quantity>& control_points, casacore::Quantity& rotation) {
    // Convert casacore Record to ellipse Quantity control points
    // RegionState needed to check if bmaj/bmin swapped for LCEllipsoid
    casacore::Vector<casacore::Float> center = region_record.asArrayFloat("center");
    casacore::Vector<casacore::Float> radii = region_record.asArrayFloat("radii");
    casacore::Float theta = region_record.asFloat("theta"); // radians
    rotation = casacore::Quantity(theta, "rad");
    rotation.convert("deg"); // CASA rotang, from x-axis

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
            rotation += 90.0;
            if (rotation.getValue() > 360.0) {
                rotation -= 360.0;
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
        casacore::Vector<casacore::Double> world_coords = _coord_sys->toWorld(pixel_coords);
        casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
        control_points.push_back(casacore::Quantity(world_coords(0), world_units(0)));
        control_points.push_back(casacore::Quantity(world_coords(1), world_units(1)));

        // Convert (lattice region) axes pixel to world and add to control points
        casacore::Quantity bmaj = _coord_sys->toWorldLength(radii(0), 0);
        casacore::Quantity bmin = _coord_sys->toWorldLength(radii(1), 1);
        // Restore original axes order; oddly, rotation angle was not changed
        if (reversed) {
            control_points.push_back(bmin);
            control_points.push_back(bmaj);
            rotation += 90.0;
            if (rotation.getValue() > 360.0) {
                rotation -= 360.0;
            }
        } else {
            control_points.push_back(bmaj);
            control_points.push_back(bmin);
        }
        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Export error: ellipse Record conversion failed:" << err.getMesg() << std::endl;
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
        if (_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
            // Make x and y world coord Vectors
            casacore::Vector<casacore::Double> x_world = world_coords.row(0);
            casacore::Vector<casacore::Double> y_world = world_coords.row(1);

            // Convert x and y Vectors to Quantities and add to control_points
            casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
            for (auto i = 0; i < npoints; ++i) {
                control_points.push_back(casacore::Quantity(x_world(i), world_units(0)));
                control_points.push_back(casacore::Quantity(y_world(i), world_units(1)));
            }
            return true;
        } else {
            return false;
        }
    } catch (const casacore::AipsError& err) {
        std::cerr << "Export error: polygon Record conversion failed:" << err.getMesg() << std::endl;
        return false;
    }
}
