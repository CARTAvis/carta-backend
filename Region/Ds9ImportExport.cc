//# Ds9ImportExport.cc: import and export regions in DS9 format

#include "Ds9ImportExport.h"

// #include <casacore/casa/Quanta/QMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#include "../Util.h"

using namespace carta;

Ds9ImportExport::Ds9ImportExport(
    std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _direction_ref_frame(""), _file_pixel_coord(true), _file_id(file_id) {
    // Map from Ds9 to casacore keywords
    InitDs9CoordMap();

    // Create vector of file lines, delimited with newline or semicolon
    std::ifstream ds9_file;
    ds9_file.open(filename);
    std::vector<std::string> file_lines;
    while (!ds9_file.eof()) {
        std::string single_line;
        getline(ds9_file, single_line); // get by newline
        std::vector<std::string> lines;
        SplitString(single_line, ';', lines); // split compound line by semicolon
        for (auto& line : lines) {
            file_lines.push_back(line);
        }
    }
    ds9_file.close();

    // Process into regions
    ProcessFileLines(file_lines);
}

Ds9ImportExport::Ds9ImportExport(
    const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _direction_ref_frame(""), _file_pixel_coord(true), _file_id(file_id) {
    // Map from Ds9 to casacore keywords
    InitDs9CoordMap();

    // Create vector of file lines, delimited with newline or semicolon
    std::vector<std::string> file_lines, input_lines;
    SplitString(contents, '\n', input_lines); // lines split by newline
    for (auto single_line : input_lines) {
        std::vector<std::string> lines;
        SplitString(single_line, ';', lines); // lines split by semicolon
        for (auto& line : lines) {
            file_lines.push_back(line);
        }
    }

    // Process into regions
    ProcessFileLines(file_lines);
}

/* TODO: for export
Ds9ImportExport::Ds9ImportExport(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord)
    : _coord_sys(image_coord_sys), _pixel_coord(pixel_coord) {
    // Used for exporting regions
    InitDs9CoordMap();

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

std::vector<RegionState> Ds9ImportExport::GetImportedRegions(std::string& error) {
    error = _import_errors;
    return _regions;
}

// Process file import

void Ds9ImportExport::ProcessFileLines(std::vector<std::string>& lines) {
    // Process or ignore each file line
    if (lines.empty()) {
        return;
    }

    bool ds9_coord_sys_ok(true); // flag for invalid coord sys lines
    for (auto& line : lines) {
        // skip blank line
        if (line.empty()) {
            continue;
        }
        // skip comment
        if (line[0] == '#') {
            continue;
        }
        // skip regions excluded for later analysis (annotation-only)
        if (line[0] == '-') {
            continue;
        }
        // skip global settings not used in carta
        if (line.find("global") != std::string::npos) {
            continue;
        }

        // process coordinate system; global or for a region definition
        if (IsDs9CoordSysKeyword(line)) {
            ds9_coord_sys_ok = SetDirectionRefFrame(line);
            if (!ds9_coord_sys_ok) {
                std::string csys_error = "coord sys " + line + " not supported";
                AddImportError(csys_error);
                std::cerr << "Ds9ImportExport import error: " << csys_error << std::endl;
            }
            continue;
        }

        if (ds9_coord_sys_ok) { // else skip lines defined in that coord sys
            // direction frame is required to set regions
            if (_direction_ref_frame.empty()) {
                // Set to ctor coord sys frame
                InitializeDirectionReferenceFrame();
            }

            // process region
            // SetRegion(line);
        }
    }
}

// Coordinate system handlers

void Ds9ImportExport::InitDs9CoordMap() {
    // for converting coordinate system from DS9 to casacore
    _coord_map["physical"] = "";
    _coord_map["image"] = "";
    _coord_map["b1950"] = "B1950";
    _coord_map["fk4"] = "B1950";
    _coord_map["j2000"] = "J2000";
    _coord_map["fk5"] = "J2000";
    _coord_map["galactic"] = "GALACTIC";
    _coord_map["ecliptic"] = "ECLIPTIC";
    _coord_map["icrs"] = "ICRS";
    _coord_map["wcs"] = "UNSUPPORTED";
    _coord_map["wcsa"] = "UNSUPPORTED";
    _coord_map["linear"] = "UNSUPPORTED";
}

bool Ds9ImportExport::IsDs9CoordSysKeyword(std::string& input_line) {
    // Check if region file has coordinate in map
    std::string input_lower(input_line);
    std::transform(input_line.begin(), input_line.end(), input_lower.begin(), ::tolower); // convert to lowercase
    return _coord_map.count(input_lower);
}

bool Ds9ImportExport::SetDirectionRefFrame(std::string& ds9_coord) {
    // Convert Ds9 coord string to casacore reference frame
    // Returns whether conversion was successful or undefined/not supported
    bool converted(false);
    std::transform(ds9_coord.begin(), ds9_coord.end(), ds9_coord.begin(), ::tolower); // convert in-place to lowercase

    if (_coord_map.count(ds9_coord)) {
        if (_coord_map[ds9_coord] == "UNSUPPORTED") {
            return converted;
        }

        if ((ds9_coord != "physical") && (ds9_coord != "image")) {
            // these keywords indicate pixel coordinates; pixel is assumed until ds9 coord found
            _file_pixel_coord = false;
        }

        _direction_ref_frame = _coord_map[ds9_coord];
        converted = true;
    }

    return converted;
}

void Ds9ImportExport::InitializeDirectionReferenceFrame() {
    // Set _direction_reference_frame attribute to image coord sys direction frame
    if (_coord_sys.hasDirectionCoordinate()) {
        casacore::MDirection::Types reference_frame;
        reference_frame = _coord_sys.directionCoordinate().directionType();
        _direction_ref_frame = casacore::MDirection::showType(reference_frame);
    } else if (_coord_sys.hasLinearCoordinate()) {
        _direction_ref_frame = "linear";
    } else {
        _direction_ref_frame = "physical";
    }
}

// Import regions into RegionState vector

void Ds9ImportExport::SetRegion(std::string& region_description) {
    // Convert ds9 region description into RegionState

    // Split into region definition, properties
    std::string region_definition(region_description), region_properties;
    if (region_description.find("#") != std::string::npos) {
        std::vector<string> region_parts;
        SplitString(region_description, '#', region_parts);
        region_definition = region_parts[0];
        region_properties = region_parts[1];
    }

    // Process region definition include/exclude
    bool exclude_region(false);
    casacore::String formatted_region(region_definition); // handy utilities: trim, gsub (global substitution)
    formatted_region.trim();                              // remove beginning and ending whitespace
    formatted_region.ltrim('+');                          // remove 'include' property
    if ((formatted_region[0] == '!') || (formatted_region[0] == '-')) {
        exclude_region = true;
        formatted_region.ltrim('!'); // remove 'exclude' property
        formatted_region.ltrim('-'); // remove 'exclude' property
    }

    // Retrieve label from region properties
    casacore::String label = GetRegionName(region_properties);

    // Create Annotation Region based on type
    // ProcessRegion(ann_region_type, region_definition, label, exclude_region);
}

bool Ds9ImportExport::ParseRegion(std::string& region_definition, std::vector<std::string>& parameters, int nparams) {
    // Parse region definition into known number of parameters; first is region type.
    // DS9 can have 3 formats: optional commas, and optional parentheses
    // Ex: "circle 100 100 10", "circle(100 100 10)", "circle(100,100,10)"
    // nparams = 0 for variable number of params.
    // Returns whether syntax was parsed into nparams.
    casacore::String definition(region_definition);
    if (definition.freq('(') != definition.freq(')')) {
        return false;
    }

    definition.gsub("(", " "); // replace ( with space
    definition.gsub(")", " "); // replace ) with space
    definition.gsub(",", " "); // replace , with space
    SplitString(definition, ' ', parameters);
    if (nparams > 0) {
        return (parameters.size() == nparams);
    } else {
        return true;
    }
}

casacore::String Ds9ImportExport::GetRegionName(std::string& region_properties) {
    // Parse region properties (everything after '#') for text, used as region name
    casacore::String text_label;
    if (region_properties.empty()) {
        return text_label;
    }

    if (region_properties.find("text") != std::string::npos) {
        casacore::String properties(region_properties);
        text_label = properties.after("text=");
        // strip delimiters
        char text_delim = text_label[0];
        text_label.ltrim(text_delim);
        if (text_delim == '{') {
            text_delim = '}';
        }
        text_label = text_label.before(text_delim); // e.g. "Region X"
    }
    return text_label;
}

bool Ds9ImportExport::CheckAndConvertParameter(std::string& parameter, const std::string& region_type) {
    // Replace ds9 unit with casacore unit in value-unit parameter string, for casacore::Quantity.
    // Returns whether valid ds9 parameter
    bool valid(false);
    std::string error_prefix(region_type + " invalid parameter ");

    // use stod to find index of unit in string (after numeric value)
    size_t idx;
    try {
        double val = stod(parameter, &idx); // string to double
    } catch (std::invalid_argument& err) {
        std::string invalid_arg(error_prefix + parameter + ", not a numeric value");
        AddImportError(invalid_arg);
        std::cerr << "Ds9ImportExport import error: " << invalid_arg << std::endl;
        return valid;
    }

    size_t param_length(parameter.length());
    valid = (param_length == idx); // no unit is valid
    if (!valid) {
        // check unit/format
        if (param_length == (idx + 1)) {
            // DS9 units are a single character
            const char unit = parameter.back();
            std::string casacore_unit;
            if (unit == 'd') {
                casacore_unit = "deg";
                valid = true;
            } else if (unit == 'r') {
                casacore_unit = "rad";
                valid = true;
            } else if (unit == 'p') {
                casacore_unit = "pix";
                valid = true;
            } else if (unit == 'i') {
                casacore_unit = "pix";
                valid = true;
            } else if ((unit == '"') || (unit == '\'')) {
                // casacore unit for min, sec is the same
                valid = true;
            } else {
                std::string invalid_unit(error_prefix + "unit " + parameter);
                AddImportError(invalid_unit);
                std::cerr << "Ds9ImportExport import error: " << invalid_unit << std::endl;
                valid = false;
            }

            if (!casacore_unit.empty()) {
                // replace DS9 unit with casacore unit
                parameter.pop_back();
                parameter.append(casacore_unit);
            }
        } else {
            // check for hms, dms formats
            const char* param_carray = parameter.c_str();
            float h, m, s;
            valid = ((sscanf(param_carray, "%f:%f:%f", &h, &m, &s) == 3) || (sscanf(param_carray, "%fh%fm%fs", &h, &m, &s) == 3) ||
                     (sscanf(param_carray, "%fd%fm%fs", &h, &m, &s) == 3));
            if (!valid) {
                // Unit not a single character or time/angle format
                std::string invalid_unit(error_prefix + "unit " + parameter);
                AddImportError(invalid_unit);
                std::cerr << "Ds9ImportExport import error: " << invalid_unit << std::endl;
            }
        }
    }
    return valid;
}

casacore::String Ds9ImportExport::ConvertTimeFormatToDeg(std::string& parameter) {
    // If parameter is in sexagesimal format dd:mm::ss.ssss, convert to angle format dd.mm.ss.ssss for readQuantity
    casacore::String converted_format(parameter);
    if (converted_format.contains(":")) {
        converted_format.gsub(":", ".");
    }
    return converted_format;
}

void Ds9ImportExport::AddImportError(std::string& error) {
    // append error string
    if (_import_errors.empty()) {
        _import_errors.append("Ds9ImportExport warning: ");
    } else {
        _import_errors.append(", ");
    }
    _import_errors.append(error);
}

/*
// For export

void Ds9ImportExport::AddRegion(
    const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation) {
    RegionState state(name, type, control_points, rotation);
    _regions.push_back(state);
}

void Ds9ImportExport::PrintHeader(std::ostream& os) {
    // print file format, globals, and coord sys
    os << "# Region file format: DS9 CARTA " << VERSION_ID << std::endl;
    Ds9Properties globals;
    os << "global color=" << globals.color << " delete=" << globals.delete_region << " edit=" << globals.edit_region
       << " fixed=" << globals.fixed_region << " font=\"" << globals.font << "\" highlite=" << globals.highlite_region
       << " include=" << globals.include_region << " move=" << globals.move_region << " select=" << globals.select_region << std::endl;
    os << _direction_ref_frame << std::endl;
}

void Ds9ImportExport::PrintRegion(unsigned int i, std::ostream& os) {
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

void Ds9ImportExport::PrintRegionsToFile(std::ofstream& ofs) {
    PrintHeader(ofs);
    for (unsigned int i = 0; i < NumRegions(); ++i) {
        PrintRegion(i, ofs);
    }
}

void Ds9ImportExport::PrintBoxRegion(const RegionProperties& properties, std::ostream& os) {
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

void Ds9ImportExport::PrintEllipseRegion(const RegionProperties& properties, std::ostream& os) {
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

void Ds9ImportExport::PrintPointRegion(const RegionProperties& properties, std::ostream& os) {
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

void Ds9ImportExport::PrintPolygonRegion(const RegionProperties& properties, std::ostream& os) {
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
