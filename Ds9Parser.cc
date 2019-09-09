//# Ds9Parser: parses lines of input ds9 region file into CARTA::RegionInfo for import

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>

#include "Ds9Parser.h"
#include "Util.h"

using namespace carta;

Ds9Parser::Ds9Parser(std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys),
      _image_shape(image_shape),
      _direction_ref_frame(""),
      _DEFAULT_UNIT("deg"),
      _pixel_coord(true),
      _region_list(image_coord_sys, image_shape) {
    // Parse given file into casa::AsciiAnnotationFileLines

    // Create vector of file lines, delimited with newline or semicolon
    std::ifstream ds9_file(filename);
    if (ds9_file.is_open()) {
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
    } else {
        throw casacore::AipsError("Cannot open file");
    }
}

Ds9Parser::Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys),
      _image_shape(image_shape),
      _direction_ref_frame(""),
      _DEFAULT_UNIT("deg"),
      _pixel_coord(true),
      _region_list(image_coord_sys, image_shape) {
    // Convert given file contents into casa::AsciiAnnotationFileLines

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

        // process coordinate system
        if (IsDs9CoordSysKeyword(line)) { 
            ds9_coord_sys_ok = SetDirectionRefFrame(line);
            if (!ds9_coord_sys_ok) {
                std::cerr << "Cannot process DS9 coordinate system: " << line << std::endl;
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

void Ds9Parser::SetAnnotationRegion(std::string& region_description) {
    // Convert ds9 region description into AsciiAnnotationFileLine and add to RegionTextList.

    // Split into region parts: definition [0], properties [1]
    std::vector<string> region_parts;
    SplitString(region_description, '#', region_parts);

    // Split definition to check for annulus (e.g. "ellipse1 & !ellipse2")
    std::vector<string> region_definitions;
    SplitString(region_parts[0], '&', region_definitions);
    if (region_definitions.size() == 2) {
        std::cerr << "Import error: Ellipse Annulus and Box Annulus not supported" << std::endl;
        return;
    }

    // Process region definition
    // DS9 can have 3 formats: optional commas, and optional parentheses
    // Ex: "circle 100 100 10", "circle(100 100 10)", "circle(100,100,10)"
    bool exclude_region(false);
    casacore::String formatted_region(region_definitions[0]); // handy utilities: trim, gsub (global substitution)
    formatted_region.trim();                                  // remove beginning and ending whitespace
    formatted_region.ltrim('+');                              // remove 'include' property
    if ((formatted_region[0] == '!') || (formatted_region[0] == '-')) {
        exclude_region = true;
        formatted_region.ltrim('!'); // remove 'exclude' property
        formatted_region.ltrim('-'); // remove 'exclude' property
    }

    // normalize all formats into space delimiter "circle 100 100 10":
    formatted_region.gsub("(", " "); // replace left parenthesis with space
    formatted_region.gsub(")", "");  // remove right parenthesis
    formatted_region.gsub(",", " "); // replace commas with space

    // split definition into parts (region type and parameters) e.g. ["circle", "100", "100", "10"] 
    std::vector<std::string> region_parameters;
    SplitString(formatted_region, ' ', region_parameters);
    if (region_parameters.size() < 3) {
        return;
    }

    // convert to casacore units
    ConvertUnits(region_parameters);

    // process region properties (currently text only)
    casacore::String label;
    if (region_parts.size() > 1) {
        label = GetTextLabel(region_parts[1]);
    }

    ProcessRegionDefinition(region_parameters, label, exclude_region);
}

// Coordinate system helpers

bool Ds9Parser::IsDs9CoordSysKeyword(std::string& input) {
    const std::string ds9_coordsys_keywords[] = {
        "PHYSICAL", "IMAGE", "FK4", "B1950", "FK5", "J2000", "GALACTIC", "ECLIPTIC", "ICRS", "LINEAR", "AMPLIFIER", "DETECTOR"};
    std::string input_upper(input);
    std::transform(input.begin(), input.end(), input_upper.begin(), ::toupper); // convert to uppercase
    for (auto& keyword : ds9_coordsys_keywords) {
        if (keyword == input_upper) {
            return true;
        }
    }
    return false;
}

bool Ds9Parser::SetDirectionRefFrame(std::string& ds9_coord) {
    // Convert coord sys string to CRTF-defined reference frame
    // Returns whether conversion was successful or undefined/not supported
    bool converted_coord(false);
    std::transform(ds9_coord.begin(), ds9_coord.end(), ds9_coord.begin(), ::toupper); // convert in-place to uppercase
    if ((ds9_coord == "PHYSICAL") || (ds9_coord == "IMAGE")) { // pixel
        converted_coord = true;
    } else if ((ds9_coord == "FK4") || (ds9_coord == "B1950")) {
        _direction_ref_frame = "B1950";
        _pixel_coord = false;
        converted_coord = true;
    } else if ((ds9_coord == "FK5") || (ds9_coord == "J2000")) {
        _direction_ref_frame = "J2000";
        _pixel_coord = false;
        converted_coord = true;
    } else if ((ds9_coord == "GALACTIC") || (ds9_coord == "ECLIPTIC") || (ds9_coord == "ICRS")) {
        _direction_ref_frame = ds9_coord;
        _pixel_coord = false;
        converted_coord = true;
    }
    return converted_coord;
}

void Ds9Parser::InitializeDirectionReferenceFrame() {
    // Set _direction_reference_frame attribute to image coord sys direction frame
    casacore::MDirection::Types reference_frame_type(casacore::MDirection::DEFAULT);
    if (_coord_sys.hasDirectionCoordinate()) {
        _coord_sys.directionCoordinate().getReferenceConversion(reference_frame_type);
    }
    _direction_ref_frame = casacore::MDirection::showType(reference_frame_type);
}

void Ds9Parser::ConvertUnits(std::vector<std::string>& region_parameters) {
    // replace ds9 units with casacore units
    for (size_t i = 1; i < region_parameters.size(); ++i) {
        std::string casacore_unit;
        if (region_parameters[i].back() == 'd') {
            casacore_unit = "deg";
        } else if (region_parameters[i].back() == 'r') {
            casacore_unit = "rad";
        } else if (region_parameters[i].back() == 'p') {
            casacore_unit = "pix";
        } else if (region_parameters[i].back() == 'i') {
            casacore_unit = "pix";
        }
        if (!casacore_unit.empty()) {
            region_parameters[i].pop_back();
            region_parameters[i].append(casacore_unit);
        }
    }
}

casacore::String Ds9Parser::GetTextLabel(std::string& region_properties) {
    // Parse region properties (everything after '#') for text
    casacore::String text_label;
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

// Region helpers

void Ds9Parser::ProcessRegionDefinition(std::vector<std::string>& region_definition, casacore::String& label, bool exclude_region) {
    // Process region definition vector (e.g. ["circle", "100", "100", "20"]) into AnnRegion
    std::string region_type = region_definition[0];
    // point can have symbol descriptor, e.g. ""circle point", "diamond point", etc.
    if (region_definition[1] == "point") {
        region_type = "point";
    }
    // Get Annotation type from region type
    casa::AnnotationBase::Type ann_region_type;
    if (!GetAnnotationRegionType(region_type, ann_region_type)) {
        return;
    }

    // process region type parameters into AnnRegion or AnnSymbol
    std::string error_message;
    casa::AnnRegion* ann_region(nullptr);
    casa::AnnSymbol* ann_symbol(nullptr); // not a region, handle separately
    try {
        switch (ann_region_type) {
            case casa::AnnotationBase::Type::CIRCLE:
                ann_region = CreateCircleRegion(region_definition);
                break;
            case casa::AnnotationBase::Type::ELLIPSE:
                ann_region = CreateEllipseRegion(region_definition);
                break;
            case casa::AnnotationBase::Type::ROTATED_BOX: // or a CENTER_BOX if angle==0
                ann_region = CreateBoxRegion(region_definition);
                break;
            case casa::AnnotationBase::Type::POLYGON:
                ann_region = CreatePolygonRegion(region_definition);
                break;
            case casa::AnnotationBase::Type::SYMBOL: // used for carta point region
                ann_symbol = CreateSymbolRegion(region_definition);
                break;
            case casa::AnnotationBase::Type::ANNULUS:
            case casa::AnnotationBase::Type::LINE:
                error_message = "Import region " + region_type + " failed:  not supported yet.";
                break;
            case casa::AnnotationBase::Type::TEXT:
                error_message = "Import region " + region_type + " failed:  annotations not supported yet.";
            default:
                break;
        }
    } catch (casacore::AipsError& err) {
        std::ostringstream oss;
        oss << "Import region " << region_type << " failed: " << err.getMesg();
        error_message = oss.str();
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
        std::cerr << error_message << std::endl;
        return;
    }
    casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
    _region_list.addLine(file_line);
}

bool Ds9Parser::GetAnnotationRegionType(std::string& ds9_region, casa::AnnotationBase::Type& type) {
    // Convert ds9 region type (everything but "ruler") to annotation region type.
    // Returns whether conversion was successful, there is no AnnotationBase::Type::UNKNOWN!
    bool found_type(false);
    if (ds9_region == "circle") {
        type = casa::AnnotationBase::Type::CIRCLE;
        found_type = true;
    } else if (ds9_region == "annulus") {
        type = casa::AnnotationBase::Type::ANNULUS;
        found_type = true;
    } else if (ds9_region == "ellipse") {
        type = casa::AnnotationBase::Type::ELLIPSE;
        found_type = true;
    } else if (ds9_region == "box") {
        type = casa::AnnotationBase::Type::ROTATED_BOX; // or a CENTER_BOX if angle=0
        found_type = true;
    } else if (ds9_region == "polygon") {
        type = casa::AnnotationBase::Type::POLYGON;
        found_type = true;
    } else if (ds9_region == "line") {
        type = casa::AnnotationBase::Type::LINE;
        found_type = true;
    } else if (ds9_region == "text") {
        type = casa::AnnotationBase::Type::TEXT;
        found_type = true;
    } else if (ds9_region == "point") {
        type = casa::AnnotationBase::Type::SYMBOL;
        found_type = true;
    }
    return found_type;
}

casa::AnnRegion* Ds9Parser::CreateCircleRegion(std::vector<std::string>& region_definition) {
    // Create AnnCircle from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    if (region_definition.size() == 4) { // circle x y radius
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
        for (auto& param_string : region_definition) {
            casacore::Quantity param_quantity;
            if (readQuantity(param_quantity, param_string)) {
                if (param_quantity.getUnit().empty()) {
                    param_quantity.setUnit("pix");
                }
                parameters.push_back(param_quantity);
            } else {
                std::cerr << "ERROR: cannot process circle parameter " << param_string << std::endl;
                return ann_region; // nullptr, cannot process parameters
            }
        }

        // AnnCircle arguments
        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        bool annotation_only(false);

        ann_region = new casa::AnnCircle(parameters[0], parameters[1], parameters[2], _direction_ref_frame, _coord_sys, _image_shape,
            begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types, annotation_only);
    }

    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreateEllipseRegion(std::vector<std::string>& region_definition) {
    // Create AnnEllipse from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    size_t nparams(region_definition.size());
    if (nparams == 6) { // ellipse x y radius radius angle
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
        for (size_t i = 1; i < nparams; ++i) {
            std::string param_string(region_definition[i]);
            casacore::Quantity param_quantity;
            if (readQuantity(param_quantity, param_string)) {
                if (param_quantity.getUnit().empty()) {
                    if ((i == nparams - 1) || (!_pixel_coord)) {
                        param_quantity.setUnit(_DEFAULT_UNIT);
                    } else {
                        param_quantity.setUnit("pix");
                    }
                }
                parameters.push_back(param_quantity);
            } else {
                std::cerr << "ERROR: cannot process ellipse parameter " << param_string << std::endl;
                return ann_region; // nullptr, cannot process parameters
            }
        }

        // AnnEllipse arguments
        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        bool annotation_only(false);

        ann_region = new casa::AnnEllipse(parameters[0], parameters[1], parameters[2], parameters[3], parameters[4], _direction_ref_frame,
            _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types, annotation_only);
    }

    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreateBoxRegion(std::vector<std::string>& region_definition) {
    // Create AnnCenterBox or AnnRotBox from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    size_t nparams(region_definition.size());
    if (nparams == 6) { // box x y width height angle
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
        for (size_t i = 1; i < nparams; ++i) {
            std::string xy_string(region_definition[i]);
            casacore::Quantity xy_quantity;
            if (readQuantity(xy_quantity, xy_string)) {
                if (xy_quantity.getUnit().empty()) {
                    if ((i == nparams - 1) || (!_pixel_coord)) {
                        xy_quantity.setUnit(_DEFAULT_UNIT);
                    } else {
                        xy_quantity.setUnit("pix");
                    }
                }
                parameters.push_back(xy_quantity);
            } else {
                std::cerr << "ERROR: cannot process box parameter " << xy_string << std::endl;
                return ann_region; // nullptr, cannot process parameters
            }
        }

        // AnnCenterBox / AnnRotBox arguments
        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        bool annotation_only(false);

        if (parameters[4].getValue() == 0.0) { // angle parameter; no rotation
            ann_region = new casa::AnnCenterBox(parameters[0], parameters[1], parameters[2], parameters[3], _direction_ref_frame,
                _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types, annotation_only);
        } else {
            ann_region = new casa::AnnRotBox(parameters[0], parameters[1], parameters[2], parameters[3], parameters[4],
                _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq, freq_ref_frame, doppler, rest_freq, stokes_types,
                annotation_only);
        }
    }
    return ann_region;
}

casa::AnnRegion* Ds9Parser::CreatePolygonRegion(std::vector<std::string>& region_definition) {
    // Create AnnPolygon from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    if (region_definition.size() % 2 == 1) { // "polygon" + pairs of x,y points
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
        for (size_t i = 1; i < region_definition.size(); i++) {
            std::string xy_string(region_definition[i]);
            casacore::Quantity xy_quantity;
            if (readQuantity(xy_quantity, xy_string)) {
                if (xy_quantity.getUnit().empty()) {
                    if (!_pixel_coord) {
                        xy_quantity.setUnit(_DEFAULT_UNIT);
                    } else {
                        xy_quantity.setUnit("pix");
                    }
                }
                parameters.push_back(xy_quantity);
            } else {
                std::cerr << "ERROR: cannot process polygon parameter " << xy_string << std::endl;
                return ann_region; // nullptr, cannot process parameters
            }
        }

        // AnnPolygon arguments
        std::vector<casacore::Quantity> xPositions, yPositions;
        for (size_t i = 0; i < parameters.size(); i += 2) {
            xPositions.push_back(parameters[i]);
            yPositions.push_back(parameters[i + 1]);
        }
        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;
        bool annotation_only(false);

        ann_region = new casa::AnnPolygon(xPositions, yPositions, _direction_ref_frame, _coord_sys, _image_shape, begin_freq, end_freq,
            freq_ref_frame, doppler, rest_freq, stokes_types, annotation_only);
    }
 
    return ann_region;
}

casa::AnnSymbol* Ds9Parser::CreateSymbolRegion(std::vector<std::string>& region_definition) {
    // Create AnnSymbol from DS9 region definition
    casa::AnnSymbol* ann_symbol(nullptr);
    if (region_definition.size() == 3) { // point x y
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
        for (size_t i = 1; i < region_definition.size(); ++i) {
            std::string xy_string(region_definition[i]);
            casacore::Quantity xy_quantity;
            if (readQuantity(xy_quantity, xy_string)) {
                if (xy_quantity.getUnit().empty()) {
                    if (!_pixel_coord) {
                        xy_quantity.setUnit(_DEFAULT_UNIT);
                    } else {
                        xy_quantity.setUnit("pix");
                    }
                }
                parameters.push_back(xy_quantity);
            } else {
                std::cerr << "ERROR: cannot process point parameter " << xy_string << std::endl;
                return ann_symbol; // nullptr, cannot process parameters
            }
        }

        // AnnSymbol arguments
        casacore::Quantity begin_freq, end_freq, rest_freq;
        casacore::String freq_ref_frame, doppler;
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types;

        ann_symbol = new casa::AnnSymbol(parameters[0], parameters[1], _direction_ref_frame, _coord_sys, '.', begin_freq, end_freq,
            freq_ref_frame, doppler, rest_freq, stokes_types);
    }

    return ann_symbol;
}
