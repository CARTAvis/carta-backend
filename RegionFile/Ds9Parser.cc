//# Ds9Parser: parses lines of input ds9 region file into CARTA::RegionInfo for import

#include <iomanip>

#include <casacore/casa/Quanta/QMath.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>

#include "../Util.h"
#include "Ds9Parser.h"

using namespace carta;

Ds9Parser::Ds9Parser(std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys),
      _image_shape(image_shape),
      _direction_ref_frame(""),
      _pixel_coord(true),
      _region_list(image_coord_sys, image_shape) {
    // Parse given file into casa::AsciiAnnotationFileLines
    InitDs9CoordMap();

    // Create vector of file lines, delimited with newline or semicolon
    std::ifstream ds9_file;
    ds9_file.open(filename);
    std::vector<std::string> file_lines;
    while (!ds9_file.eof()) {
        std::string single_line;
        getline(ds9_file, single_line); // get by newline
        std::vector<std::string> lines;
        SplitString(single_line, ';', lines); // split by semicolon
        for (auto& line : lines) {
            file_lines.push_back(line);
        }
    }
    ds9_file.close();
    // Process into annotation lines
    ProcessFileLines(file_lines);
}

Ds9Parser::Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys),
      _image_shape(image_shape),
      _direction_ref_frame(""),
      _pixel_coord(true),
      _region_list(image_coord_sys, image_shape) {
    // Convert given file contents into casa::AsciiAnnotationFileLines
    InitDs9CoordMap();

    // Create vector of file lines, delimited with newline or semicolon
    std::vector<std::string> file_lines, input_lines;
    SplitString(contents, '\n', input_lines); // split by newline
    for (auto single_line : input_lines) {
        std::vector<std::string> lines;
        SplitString(single_line, ';', lines); // split by semicolon
        for (auto& line : lines) {
            file_lines.push_back(line);
        }
    }
    // Process into annotation lines
    ProcessFileLines(file_lines);
}

Ds9Parser::Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord)
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

void Ds9Parser::InitDs9CoordMap() {
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

// public accessors

unsigned int Ds9Parser::NumLines() {
    return _region_list.nLines();
}

const casacore::Vector<casa::AsciiAnnotationFileLine> Ds9Parser::GetLines() {
    return _region_list.getLines();
}

casa::AsciiAnnotationFileLine Ds9Parser::LineAt(unsigned int i) {
    // region list throws exception if index out of range
    return _region_list.lineAt(i);
}

// Process or ignore each file line

void Ds9Parser::ProcessFileLines(std::vector<std::string>& lines) {
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
                std::cerr << "Ds9Parser import error: " << csys_error << std::endl;
            }
            continue;
        }

        if (ds9_coord_sys_ok) { // else skip lines defined in that coord sys
            // direction frame required to set regions
            if (_direction_ref_frame.empty()) {
                InitializeDirectionReferenceFrame();
            }
            // process region
            SetAnnotationRegion(line);
        }
    }
}

// Coordinate system helpers

bool Ds9Parser::IsDs9CoordSysKeyword(std::string& input) {
    std::string input_lower(input);
    std::transform(input.begin(), input.end(), input_lower.begin(), ::tolower); // convert to lowercase
    return _coord_map.count(input_lower);
}

bool Ds9Parser::SetDirectionRefFrame(std::string& ds9_coord) {
    // Convert coord sys string to CRTF-defined reference frame
    // Returns whether conversion was successful or undefined/not supported
    bool converted_coord(false);
    std::transform(ds9_coord.begin(), ds9_coord.end(), ds9_coord.begin(), ::tolower); // convert in-place to lowercase

    if (_coord_map.count(ds9_coord)) {
        if (_coord_map[ds9_coord] == "UNSUPPORTED") {
            return converted_coord;
        }

        if ((ds9_coord != "physical") && (ds9_coord != "image")) { // pixel coordinates
            _pixel_coord = false;
        }

        _direction_ref_frame = _coord_map[ds9_coord];
        converted_coord = true;
    }

    return converted_coord;
}

void Ds9Parser::InitializeDirectionReferenceFrame() {
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

// Create Annotation region

void Ds9Parser::SetAnnotationRegion(std::string& region_description) {
    // Convert ds9 region description into AsciiAnnotationFileLine and add to RegionTextList.

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

    // Get Annotation type from region definition
    casa::AnnotationBase::Type ann_region_type;
    if (!GetAnnotationRegionType(region_definition, ann_region_type)) {
        std::string unsupported_type("unknown/unsupported keyword " + region_definition);
        AddImportError(unsupported_type);
        std::cerr << "Ds9Parser import error: " << unsupported_type << std::endl;
        return;
    }

    // Retrieve label from region properties
    casacore::String label = GetRegionName(region_properties);

    // Create Annotation Region based on type
    ProcessRegionDefinition(ann_region_type, region_definition, label, exclude_region);
}

bool Ds9Parser::GetAnnotationRegionType(std::string& ds9_region, casa::AnnotationBase::Type& type) {
    // Convert DS9 region type to annotation region type.
    // Returns whether conversion was successful; there is no AnnotationBase::Type::UNKNOWN
    bool found_type(false);
    std::unordered_map<std::string, casa::AnnotationBase::Type> region_type_map = {{"circle", casa::AnnotationBase::Type::CIRCLE},
        {"ellipse", casa::AnnotationBase::Type::ELLIPSE}, {"box", casa::AnnotationBase::Type::ROTATED_BOX},
        {"polygon", casa::AnnotationBase::Type::POLYGON}, {"point", casa::AnnotationBase::Type::SYMBOL},
        {"line", casa::AnnotationBase::Type::LINE}, {"vector", casa::AnnotationBase::Type::VECTOR},
        {"text", casa::AnnotationBase::Type::TEXT}, {"annulus", casa::AnnotationBase::Type::ANNULUS}};

    // search for "point" first, could be "circle point", "box point", etc.
    if (ds9_region.find("point") != std::string::npos) {
        type = region_type_map["point"];
        found_type = true;
    } else {
        for (auto& region_type : region_type_map) {
            if (ds9_region.find(region_type.first) != std::string::npos) {
                type = region_type.second;
                found_type = true;
                break;
            }
        }
    }
    return found_type;
}

casacore::String Ds9Parser::GetRegionName(std::string& region_properties) {
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

void Ds9Parser::ProcessRegionDefinition(
    casa::AnnotationBase::Type ann_region_type, std::string& region_definition, casacore::String& label, bool exclude_region) {
    // Process region definition arguments into AnnRegion or AnnSymbol
    std::string error_message, region_type;
    casa::AnnRegion* ann_region(nullptr);
    casa::AnnSymbol* ann_symbol(nullptr); // not a region, handle separately
    try {
        switch (ann_region_type) {
            case casa::AnnotationBase::Type::CIRCLE: {
                region_type = "circle";
                ann_region = CreateCircleRegion(region_definition);
                break;
            }
            case casa::AnnotationBase::Type::ELLIPSE: {
                region_type = "ellipse";
                ann_region = CreateEllipseRegion(region_definition);
                break;
            }
            case casa::AnnotationBase::Type::ROTATED_BOX: { // or a CENTER_BOX if angle==0
                region_type = "box";
                ann_region = CreateBoxRegion(region_definition);
                break;
            }
            case casa::AnnotationBase::Type::POLYGON: {
                region_type = "polygon";
                ann_region = CreatePolygonRegion(region_definition);
                break;
            }
            case casa::AnnotationBase::Type::SYMBOL: { // used for carta point region
                region_type = "point";
                ann_symbol = CreateSymbolRegion(region_definition);
                break;
            }
            case casa::AnnotationBase::Type::ANNULUS: {
                region_type = "annulus";
                error_message = "Import region '" + region_type + "' failed:  not supported yet.";
                break;
            }
            case casa::AnnotationBase::Type::LINE: {
                region_type = "line";
                error_message = "Import region '" + region_type + "' failed:  not supported yet.";
                break;
            }
            case casa::AnnotationBase::Type::TEXT: {
                region_type = "text";
                error_message = "Import '" + region_type + "' failed:  annotations not supported yet.";
            }
            default:
                break;
        }
    } catch (casacore::AipsError& err) {
        std::ostringstream oss;
        oss << "Import region '" << region_type << "' failed: " << err.getMesg();
        error_message = oss.str();
    }

    if (!error_message.empty()) {
        AddImportError(error_message);
        std::cerr << "Ds9Parser import error: " << error_message << std::endl;
    }

    // Add AsciiAnnotationFileLine for Annotation to RegionTextList
    casacore::CountedPtr<const casa::AnnotationBase> annotation_region;
    if (ann_symbol != nullptr) {
        ann_symbol->setLabel(label);
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_symbol);
    } else if (ann_region != nullptr) {
        ann_region->setLabel(label);
        ann_region->setDifference(exclude_region);
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_region);
    } else {
        return;
    }

    casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
    _region_list.addLine(file_line);
}

bool Ds9Parser::CheckAndConvertParameter(std::string& parameter, const std::string& region_type) {
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
        std::cerr << "Ds9Parser import error: " << invalid_arg << std::endl;
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
                std::cerr << "Ds9Parser import error: " << invalid_unit << std::endl;
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
                std::cerr << "Ds9Parser import error: " << invalid_unit << std::endl;
            }
        }
    }
    return valid;
}

casacore::String Ds9Parser::ConvertTimeFormatToDeg(std::string& parameter) {
    // If parameter is in sexagesimal format dd:mm::ss.ssss, convert to angle format dd.mm.ss.ssss for readQuantity
    casacore::String converted_format(parameter);
    if (converted_format.contains(":")) {
        converted_format.gsub(":", ".");
    }
    return converted_format;
}

bool Ds9Parser::ParseRegion(std::string& region_definition, std::vector<std::string>& parameters, int nparams) {
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

casa::AnnRegion* Ds9Parser::CreateBoxRegion(std::string& region_definition) {
    // Create AnnCenterBox or AnnRotBox from DS9 region definition
    // box x y width height angle
    casa::AnnRegion* ann_region(nullptr);
    std::vector<std::string> params;
    if (ParseRegion(region_definition, params, 6)) {
        // convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;
        std::vector<casacore::String> units = {"", "deg", "deg", "arcsec", "arcsec", "deg"};
        size_t nparams(params.size());
        for (size_t i = 1; i < nparams; ++i) {
            std::string param(params[i]);
            if (CheckAndConvertParameter(param, "box")) {
                casacore::String param_string(param);
                if (i == 2) { // degree format, not time
                    param_string = ConvertTimeFormatToDeg(param);
                }
                casacore::Quantity param_quantity;
                if (readQuantity(param_quantity, param_string)) {
                    if (param_quantity.getUnit().empty()) {
                        if ((i == nparams - 1) || !_pixel_coord) {
                            param_quantity.setUnit(units[i]);
                        } else {
                            param_quantity.setUnit("pix");
                        }
                    }
                    param_quantities.push_back(param_quantity);
                } else {
                    std::string invalid_param("invalid box parameter " + param);
                    AddImportError(invalid_param);
                    std::cerr << "Ds9Parser import error: " << invalid_param << std::endl;
                    return ann_region;
                }
            } else {
                return ann_region;
            }
        }

        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        if (param_quantities[4].getValue() == 0.0) { // angle parameter; no rotation
            ann_region = new casa::AnnCenterBox(param_quantities[0], param_quantities[1], param_quantities[2], param_quantities[3],
                _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types,
                false, false);
        } else {
            ann_region = new casa::AnnRotBox(param_quantities[0], param_quantities[1], param_quantities[2], param_quantities[3],
                param_quantities[4], _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler,
                rest_freq, stokes_types, false, false);
        }
    } else if (ParseRegion(region_definition, params, 0)) {
        // unsupported box annulus: box x y w1 h1 w2 h2 [angle]
        std::string invalid_params = "unsupported box definition " + region_definition;
        AddImportError(invalid_params);
        std::cerr << "Ds9Parser import error: " << invalid_params << std::endl;
    } else {
        std::string syntax_error = "box syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
    }
    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreateCircleRegion(std::string& region_definition) {
    // Create AnnCircle from DS9 region definition
    // circle x y radius
    casa::AnnRegion* ann_region(nullptr);
    std::vector<std::string> params;
    if (!ParseRegion(region_definition, params, 4)) {
        std::string syntax_error = "circle syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
        return ann_region;
    }

    // convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;
    std::vector<casacore::String> units = {"", "deg", "deg", "arcsec"};
    for (size_t i = 1; i < params.size(); ++i) {
        std::string param(params[i]);
        if (CheckAndConvertParameter(param, "circle")) {
            casacore::String param_string(param);
            if (i == 2) { // degree format, not time
                param_string = ConvertTimeFormatToDeg(param);
            }
            casacore::Quantity param_quantity;
            if (readQuantity(param_quantity, param_string)) {
                if (param_quantity.getUnit().empty()) {
                    if (_pixel_coord) {
                        param_quantity.setUnit("pix");
                    } else {
                        param_quantity.setUnit(units[i]);
                    }
                }
                param_quantities.push_back(param_quantity);
            } else {
                std::string invalid_param("invalid circle parameter " + param);
                AddImportError(invalid_param);
                std::cerr << "Ds9Parser import error: " << invalid_param << std::endl;
                return ann_region;
            }
        } else {
            return ann_region;
        }
    }

    casacore::Quantity begin_freq, end_freq, rest_freq;
    casacore::String freq_ref_frame, doppler;
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
    ann_region = new casa::AnnCircle(param_quantities[0], param_quantities[1], param_quantities[2], _direction_ref_frame, _coord_sys,
        _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types, false, false);
    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreateEllipseRegion(std::string& region_definition) {
    // Create AnnEllipse from DS9 region definition
    // ellipse x y radius radius angle
    casa::AnnRegion* ann_region(nullptr);
    std::vector<std::string> params;
    if (ParseRegion(region_definition, params, 6)) {
        // convert strings to Quantities
        std::vector<casacore::Quantity> param_quantities;
        std::vector<casacore::String> units = {"", "deg", "deg", "arcsec", "arcsec", "deg"};
        size_t nparams(params.size());
        for (size_t i = 1; i < nparams; ++i) {
            std::string param(params[i]);
            if (CheckAndConvertParameter(param, "ellipse")) {
                casacore::String param_string(param);
                if (i == 2) { // degree format, not time
                    param_string = ConvertTimeFormatToDeg(param);
                }
                casacore::Quantity param_quantity;
                if (readQuantity(param_quantity, param_string)) {
                    if (param_quantity.getUnit().empty()) {
                        if ((i == nparams - 1) || !_pixel_coord) {
                            param_quantity.setUnit(units[i]);
                        } else {
                            param_quantity.setUnit("pix");
                        }
                    }
                    param_quantities.push_back(param_quantity);
                } else {
                    std::string invalid_param("invalid ellipse parameter " + param);
                    AddImportError(invalid_param);
                    std::cerr << "Ds9Parser import error: " << invalid_param << std::endl;
                    return ann_region;
                }
            } else {
                return ann_region;
            }
        }

        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        // adjust angle (from x-axis)
        casacore::Quantity position_angle(param_quantities[4]);
        position_angle -= 90.0;
        ann_region = new casa::AnnEllipse(param_quantities[0], param_quantities[1], param_quantities[2], param_quantities[3],
            position_angle, _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq,
            stokes_types, false, false);
    } else if (ParseRegion(region_definition, params, 0)) {
        // unsupported ellipse annulus: ellipse x y r11 r12 r21 r22 [angle]
        std::string invalid_params = "unsupported ellipse definition " + region_definition;
        AddImportError(invalid_params);
        std::cerr << "Ds9Parser import error: " << invalid_params << std::endl;
    } else {
        std::string syntax_error = "ellipse syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
    }
    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreatePolygonRegion(std::string& region_definition) {
    // Create AnnPolygon from DS9 region definition
    // polygon x1 y1 x2 y2 x3 y3 ...
    casa::AnnRegion* ann_region(nullptr);
    std::vector<std::string> params;
    if (!ParseRegion(region_definition, params, 0)) {
        std::string syntax_error = "polygon syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
        return ann_region;
    }

    if (params.size() % 2 != 1) {
        std::string syntax_error = "polygon syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
        return ann_region;
    }

    // convert strings to Quantities
    std::vector<casacore::Quantity> param_quantities;
    for (size_t i = 1; i < params.size(); ++i) {
        std::string param(params[i]);
        if (CheckAndConvertParameter(param, "polygon")) {
            casacore::String param_string(param);
            if ((i % 2) == 0) {
                param_string = ConvertTimeFormatToDeg(param_string);
            }
            casacore::Quantity param_quantity;
            if (readQuantity(param_quantity, param_string)) {
                if (param_quantity.getUnit().empty()) {
                    if (_pixel_coord) {
                        param_quantity.setUnit("pix");
                    } else {
                        param_quantity.setUnit("deg");
                    }
                }
                param_quantities.push_back(param_quantity);
            } else {
                std::string invalid_param("invalid polygon parameter " + param);
                AddImportError(invalid_param);
                std::cerr << "Ds9Parser import error: " << invalid_param << std::endl;
                return ann_region;
            }
        } else {
            return ann_region;
        }
    }

    // AnnPolygon arguments
    std::vector<casacore::Quantity> x_positions, y_positions;
    for (size_t i = 0; i < param_quantities.size(); i += 2) {
        x_positions.push_back(param_quantities[i]);
        y_positions.push_back(param_quantities[i + 1]);
    }
    casacore::Quantity begin_freq, end_freq, rest_freq;
    casacore::String freq_ref_frame, doppler;
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;

    ann_region = new casa::AnnPolygon(x_positions, y_positions, _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq,
        freq_ref_frame, doppler, rest_freq, stokes_types, false, false);
    return ann_region;
}

casa::AnnSymbol* Ds9Parser::CreateSymbolRegion(std::string& region_definition) {
    // Create AnnSymbol from DS9 region definition
    // point x y, circle point x y
    casa::AnnSymbol* ann_symbol(nullptr);
    std::vector<std::string> params;
    int first_param;
    if (ParseRegion(region_definition, params, 3)) {
        first_param = 1;
    } else if (ParseRegion(region_definition, params, 4)) {
        first_param = 2;
    } else {
        std::string syntax_error = "point syntax error " + region_definition;
        AddImportError(syntax_error);
        std::cerr << "Ds9Parser import error: " << syntax_error << std::endl;
        return ann_symbol;
    }

    std::vector<casacore::Quantity> param_quantities;
    for (size_t i = first_param; i < params.size(); ++i) {
        std::string param(params[i]);
        if (CheckAndConvertParameter(param, "point")) {
            casacore::String param_string(param);
            if (i == first_param + 1) {
                param_string = ConvertTimeFormatToDeg(param_string);
            }
            casacore::Quantity param_quantity;
            if (readQuantity(param_quantity, param_string)) {
                if (param_quantity.getUnit().empty()) {
                    if (_pixel_coord) {
                        param_quantity.setUnit("pix");
                    } else {
                        param_quantity.setUnit("deg");
                    }
                }
                param_quantities.push_back(param_quantity);
            } else {
                std::string invalid_param("invalid point parameter " + param);
                AddImportError(invalid_param);
                std::cerr << "Ds9Parser import error: " << invalid_param << std::endl;
                return ann_symbol;
            }
        } else {
            return ann_symbol;
        }
    }

    casacore::Quantity begin_freq, end_freq, rest_freq;
    casacore::String freq_ref_frame, doppler;
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
    ann_symbol = new casa::AnnSymbol(param_quantities[0], param_quantities[1], _direction_ref_frame, _coord_sys, '.', begin_freq, end_freq,
        freq_ref_frame, doppler, rest_freq, stokes_types);
    return ann_symbol;
}

void Ds9Parser::AddImportError(std::string& error) {
    // append error string
    if (_import_errors.empty()) {
        _import_errors.append("Ds9Parser warning: ");
    } else {
        _import_errors.append(", ");
    }
    _import_errors.append(error);
}

// For export

void Ds9Parser::AddRegion(
    const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation) {
    RegionProperties properties(name, type, control_points, rotation);
    _regions.push_back(properties);
}

void Ds9Parser::PrintHeader(std::ostream& os) {
    // print file format, globals, and coord sys
    os << "# Region file format: DS9 CARTA " << VERSION_ID << std::endl;
    Ds9Properties globals;
    os << "global color=" << globals.color << " delete=" << globals.delete_region << " edit=" << globals.edit_region
       << " fixed=" << globals.fixed_region << " font=\"" << globals.font << "\" highlite=" << globals.highlite_region
       << " include=" << globals.include_region << " move=" << globals.move_region << " select=" << globals.select_region << std::endl;
    os << _direction_ref_frame << std::endl;
}

void Ds9Parser::PrintRegion(unsigned int i, std::ostream& os) {
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

void Ds9Parser::PrintRegionsToFile(std::ofstream& ofs) {
    PrintHeader(ofs);
    for (unsigned int i = 0; i < NumRegions(); ++i) {
        PrintRegion(i, ofs);
    }
}

void Ds9Parser::PrintBoxRegion(const RegionProperties& properties, std::ostream& os) {
    // box(x,y,width,height,angle)
    std::string ds9_region("box");
    std::vector<casacore::Quantity> points = properties.control_points;
    os << ds9_region << "(";
    if (_pixel_coord) {
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

void Ds9Parser::PrintEllipseRegion(const RegionProperties& properties, std::ostream& os) {
    // ellipse(x,y,radius,radius,angle) -or- circle(x,y,radius)
    std::vector<casacore::Quantity> points = properties.control_points;
    bool is_circle(points[2].getValue() == points[3].getValue()); // bmaj == bmin
    if (is_circle) {
        os << "circle(";
        if (_pixel_coord) {
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
        if (_pixel_coord) {
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

void Ds9Parser::PrintPointRegion(const RegionProperties& properties, std::ostream& os) {
    // point(x,y)
    std::vector<casacore::Quantity> points = properties.control_points;
    os << "point(";
    if (_pixel_coord) {
        os << std::fixed << std::setprecision(2) << points[0].getValue() << "," << points[1].getValue() << ")";
    } else {
        os << std::fixed << std::setprecision(6) << points[0].get("deg").getValue() << ",";
        os << std::fixed << std::setprecision(6) << points[1].get("deg").getValue() << ")";
    }
}

void Ds9Parser::PrintPolygonRegion(const RegionProperties& properties, std::ostream& os) {
    // polygon(x1,y1,x2,y2,x3,y3,...)
    std::vector<casacore::Quantity> points = properties.control_points;
    os << "polygon(";
    if (_pixel_coord) {
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
