/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RegionImportExport.h"

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>
#include <imageanalysis/Annotations/AnnotationBase.h>

#include "../Logger/Logger.h"
#include "Util/String.h"

using namespace carta;

RegionImportExport::RegionImportExport(
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Constructor for import. Use GetImportedRegions to retrieve regions.
}

RegionImportExport::RegionImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys), _image_shape(image_shape) {
    // Constructor for export. Use AddExportRegion to add regions, then ExportRegions to finalize
}

// Public accessors

std::vector<RegionProperties> RegionImportExport::GetImportedRegions(std::string& error) {
    // Parse the file in the constructor to create RegionProperties vector; return any errors in error
    error = _import_errors;

    if ((_import_regions.size() == 0) && error.empty()) {
        error = "Import error: zero regions set. Regions may lie far outside image and cannot be converted to pixel coordinates.";
    }

    return _import_regions;
}

bool RegionImportExport::AddExportRegion(
    const RegionState& region_state, const RegionStyle& region_style, const casacore::RecordInterface& region_record, bool pixel_coord) {
    // Convert Record to Quantities for region type then set region
    // Record is in pixel coords; convert to world coords if needed
    if (pixel_coord) {
        casa::AnnotationBase::unitInit(); // enable "pix" unit
    }
    casacore::Quantity rotation(region_state.rotation, "deg");

    // Convert control points and rotation to Quantity; rotation updated for ellipse only
    std::vector<casacore::Quantity> control_points;
    bool converted(false);
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
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE: {
            converted = ConvertRecordToPolygonLine(region_record, pixel_coord, control_points);
            break;
        }
        default:
            break;
    }

    if (converted) {
        return AddExportRegion(region_state.type, region_style, control_points, rotation); // CRTF or DS9 export
    }

    return converted;
}

// Protected

std::vector<std::string> RegionImportExport::ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim) {
    // Return file lines as string vector
    std::vector<std::string> file_lines;
    if (file_is_filename) {
        std::ifstream region_file;
        region_file.open(file);
        while (!region_file.eof()) {
            std::string single_line;
            getline(region_file, single_line);

            if (!single_line.empty() && (single_line.back() == '\r')) {
                // Remove carriage return from DOS file
                single_line.pop_back();
            }

            file_lines.push_back(single_line);
        }
        region_file.close();
    } else {
        std::string contents(file);
        SplitString(contents, '\n', file_lines);
    }

    if (extra_delim == '\0') {
        return file_lines;
    }

    // Split lines by delimiter
    std::vector<std::string> split_lines;
    for (auto& line : file_lines) {
        std::vector<std::string> compound_lines;
        SplitString(line, extra_delim, compound_lines);
        for (auto& single_line : compound_lines) {
            split_lines.push_back(single_line);
        }
    }
    return split_lines;
}

void RegionImportExport::ParseRegionParameters(
    std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties) {
    // Parse the input string by space, comma, parentheses to get region parameters and properties (keyword=value)
    size_t next(0), current(0), end(region_definition.size());
    while (current < end) {
        next = region_definition.find_first_of(_parser_delim, current);
        if (next == std::string::npos) {
            next = end;
        }
        if ((next - current) > 0) {
            std::string param = region_definition.substr(current, next - current);
            if (param.find("=") == std::string::npos) {
                parameters.push_back(param);
            } else {
                std::vector<std::string> kvpair;
                SplitString(param, '=', kvpair);
                std::string key = kvpair[0];

                if (kvpair.size() == 1) {
                    // value starts with delim
                    current = next + 1;
                    if (region_definition[next] == '[') { // e.g. corr=[I, Q]
                        next = region_definition.find_first_of("]", current);
                    } else { // e.g. color=#00ffff
                        next = region_definition.find_first_of(" ", current);
                    }
                    if ((next != std::string::npos) && (next - current > 0)) {
                        std::string value = region_definition.substr(current, next - current);
                        properties[key] = value;
                    }
                } else if (kvpair.size() == 2) {
                    // check if value is delimited by ' ', " ", [ ], or { }
                    std::string value = kvpair[1];
                    if (key == "dashlist") {
                        // values separated by space e.g. "dashlist=8 3"; find next space
                        current = next + 1;
                        next = region_definition.find_first_of(" ", current);
                        string value_end = region_definition.substr(current, next - current);
                        properties[key] = value + " " + value_end;
                    } else if (value.find_first_of("'\"[{(", 0) == 0) {
                        // value delimited by special chars; find end and strip delimiters
                        char start_delim = value.front();
                        std::unordered_map<char, char> delim_map = {{'\'', '\''}, {'"', '"'}, {'[', ']'}, {'{', '}'}, {'(', ')'}};
                        char end_delim = delim_map[start_delim];

                        value.erase(0, 1); // erase start delim
                        if (value.back() == end_delim) {
                            value.pop_back();
                            properties[key] = value;
                        } else {
                            // value has parser delim in it (e.g. sp); add it and advance
                            value.append(1, region_definition[next]);
                            current = next + 1;

                            // Find ending delim for rest of value, append to value
                            next = region_definition.find_first_of(end_delim, current);
                            string value_end = region_definition.substr(current, next - current);
                            properties[key] = value.append(value_end);
                            next++; // Advance past end delim
                        }
                    } else {
                        properties[key] = value;
                    }
                }
            }
        }
        if (next < end) {
            current = next + 1;
        } else {
            current = next;
        }
    }
}

bool RegionImportExport::ConvertPointToPixels(
    std::string& region_frame, std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords) {
    // Convert point Quantities to pixels in image coord sys
    // Point is defined by 2 quantities, x and y
    if (point.size() != 2) {
        return false;
    }

    // x and y must have matched pixel/world types
    casacore::String unit0(point[0].getUnit()), unit1(point[1].getUnit());
    bool x_is_pix = unit0.contains("pix");
    bool y_is_pix = unit1.contains("pix");
    if (x_is_pix != y_is_pix) {
        return false;
    }

    // If unit is pixels, just get values
    if (x_is_pix) {
        pixel_coords.resize(2);
        pixel_coords(0) = point[0].getValue();
        pixel_coords(1) = point[1].getValue();
        return true;
    }

    // Convert world to pixel coords
    bool converted_to_pixel(false);
    if (_coord_sys->hasDirectionCoordinate()) {
        try {
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
                direction = casacore::MDirection::Convert(direction, image_direction_type)();
            }

            // Convert world to pixel coordinates. Uses wcslib wcss2p(); pixels are not fractional
            converted_to_pixel = _coord_sys->directionCoordinate().toPixel(pixel_coords, direction);
        } catch (const casacore::AipsError& err) {
            _import_errors.append("Conversion of region parameters to image coordinate system failed.\n");
            return converted_to_pixel;
        }
    }

    if (!converted_to_pixel) {
        _import_errors.append("Conversion of region parameters to image pixel coordinates failed.\n");
    }

    return converted_to_pixel;
}

double RegionImportExport::WorldToPixelLength(casacore::Quantity input, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse/circle radius or box width.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.
    if (input.getUnit() == "pix") {
        return input.getValue();
    }

    // Convert to world axis units
    casacore::Vector<casacore::String> units = _coord_sys->worldAxisUnits();
    input.convert(units[pixel_axis]);

    // Find pixel length
    casacore::Vector<casacore::Double> increments(_coord_sys->increment());
    return fabs(input.getValue() / increments[pixel_axis]);
}

std::string RegionImportExport::FormatColor(const std::string& color) {
    // Capitalize and add prefix if hex; else return same string
    std::string hex_color(color);
    if (color[0] == '#') {
        // Do conversion without prefix
        hex_color = color.substr(1);
    }

    // Check if can convert entire string to hex number
    char* endptr(nullptr);
    if (std::strtoul(hex_color.c_str(), &endptr, 16) && (*endptr == '\0')) {
        std::transform(hex_color.begin(), hex_color.end(), hex_color.begin(), ::toupper);
        hex_color = "#" + hex_color;
    }

    return hex_color;
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
        spdlog::error("Export error: point Record conversion failed: {}", err.getMesg());
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
    casacore::Double brc_x = x[1];
    casacore::Double trc_x = x[2];
    casacore::Double tlc_x = x[3];
    casacore::Double blc_y = y[0];
    casacore::Double brc_y = y[1];
    casacore::Double trc_y = y[2];
    casacore::Double tlc_y = y[3];

    // Control points: center point, width/height
    cx = (blc_x + trc_x) / 2.0;
    cy = (blc_y + trc_y) / 2.0;
    width = sqrt(pow((brc_x - blc_x), 2) + pow((brc_y - blc_y), 2));
    height = sqrt(pow((tlc_x - blc_x), 2) + pow((tlc_y - blc_y), 2));

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
        casacore::Vector<casacore::Double> pixel_center(_coord_sys->nPixelAxes(), 0.0);
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
        spdlog::error("Export error: rectangle Record conversion failed: {}", err.getMesg());
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
    pixel_coords = 0.0;
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
        spdlog::error("Export error: ellipse Record conversion failed: {}", err.getMesg());
        return false;
    }
    return false;
}

bool RegionImportExport::ConvertRecordToPolygonLine(
    const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points) {
    // Convert casacore Record to polygon Quantity control points
    // Polygon is an LCPolygon with x, y arrays in pixel coordinates
    casacore::String region_name = region_record.asString("name");
    casacore::Vector<casacore::Float> x = region_record.asArrayFloat("x");
    casacore::Vector<casacore::Float> y = region_record.asArrayFloat("y");
    size_t npoints(x.size());
    if (region_name == "LCPolygon") {
        // ignore last point, same as the first to enclose region
        npoints -= 1;
    }

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
    size_t naxes(_image_shape.size());
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
        spdlog::error("Export error: polygon Record conversion failed: {}", err.getMesg());
        return false;
    }
}
