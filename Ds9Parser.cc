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
      _pixel_coords(false),
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
      _pixel_coords(false),
      _region_list(image_coord_sys, image_shape) {
    // Convert given file contents into casa::AsciiAnnotationFileLines
    InitializeDirectionReferenceFrame();

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
            if (SetDirectionRefFrame(line)) {
                continue;
            } else {
                // TODO: skip lines until new csys definition?
                throw casacore::AipsError("Cannot process DS9 coordinate system.");
            }
        }

        // direction frame required to set regions
        if (_direction_ref_frame.empty()) {
            InitializeDirectionReferenceFrame();
        }
        // process region
        SetAnnotationRegion(line);
    }
}

void Ds9Parser::SetAnnotationRegion(std::string& region_description) {
    // Convert ds9 region description into AsciiAnnotationFileLine and add to RegionTextList.
    // Split into region definition, properties
    std::vector<string> region_parts;
    SplitString(region_description, '#', region_parts);

    // Split definition in case it is an annulus (e.g. "ellipse1 & !ellipse2")
    std::vector<string> region_definitions;
    SplitString(region_parts[0], '&', region_definitions);

    // Process region definitions
    // DS9 can have 3 formats: optional commas, and optional parentheses
    // Ex: "circle 100 100 10", "circle(100 100 10)", "circle(100,100,10)"
    for (auto& region_definition : region_definitions) {
        casacore::String formatted_region(region_definition); // handy utilities: trim, gsub (global substitution)
        formatted_region.trim();                              // remove beginning and ending whitespace
        std::string crtf_prefix;
        if (formatted_region[0] == '!') { // NOT
            crtf_prefix = "-";
            formatted_region.ltrim('!');
        }

        // normalize all formats into space delimiter "circle 100 100 10":
        formatted_region.gsub("(", " ");   // replace left parenthesis
        formatted_region.gsub(")", "");    // remove right parenthesis
        formatted_region.gsub(",", " ");   // replace commas
	// replace with casacore units
        formatted_region.gsub("d", "deg");
        formatted_region.gsub("r", "rad");
        formatted_region.gsub("p", "pix");
        formatted_region.gsub("i", "pix");
        formatted_region.gsub("'", "arcmin");
        formatted_region.gsub("\"", "arcsec");
        // TODO: hms, dms

        // split definition into parts (region type and parameters)
        std::vector<std::string> region_parameters;
        SplitString(formatted_region, ' ', region_parameters);
        if (region_parameters.size() < 3) {
            return;
        }

        ProcessRegionDefinition(region_parameters, crtf_prefix);
    }
}

// Coordinate system helpers

bool Ds9Parser::IsDs9CoordSysKeyword(std::string& input) {
    const std::string ds9_coordsys_keywords[] = {"PHYSICAL", "IMAGE", "FK4", "B1950", "FK5", "J2000", "GALACTIC", "ECLIPTIC", "ICRS", "LINEAR",
        "AMPLIFIER", "DETECTOR"};
    std::transform(input.begin(), input.end(), input.begin(), ::toupper); // convert in-place to uppercase
    for (auto& keyword : ds9_coordsys_keywords) {
        if (keyword == input) {
            return true;
        }
    }
    return false;
}

bool Ds9Parser::SetDirectionRefFrame(std::string& ds9_coord) {
    // Convert coord sys string to CRTF-defined reference frame
    // Returns whether conversion was successful
    bool converted_coord(false);
    if ((ds9_coord == "PHYSICAL") || (ds9_coord == "IMAGE")) { // pixel
        _pixel_coords = true;
        converted_coord = true;
    } else if ((ds9_coord == "FK4") || (ds9_coord == "B1950")) {
        _direction_ref_frame = "B1950";
        converted_coord = true;
    } else if ((ds9_coord == "FK5") || (ds9_coord == "J2000")) {
        _direction_ref_frame = "J2000";
        converted_coord = true;
    } else if ((ds9_coord == "GALACTIC") || (ds9_coord == "ECLIPTIC") || (ds9_coord == "ICRS")) {
        _direction_ref_frame = ds9_coord;
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

// Region helpers

void Ds9Parser::ProcessRegionDefinition(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
    // Process region definition vector (e.g. ["circle", "100", "100", "20"]) into AnnRegion
    std::string region_type = region_definition[0];
    size_t first_param_index(1);
    // point can have symbol descriptor, e.g. ""circle point", "diamond point", etc.
    if (region_definition[1] == "point") {
        region_type = "POINT";
        first_param_index = 2;
    }
    // Get Annotation type from region type
    casa::AnnotationBase::Type ann_region_type;
    bool valid_ann_region = GetAnnotationRegionType(region_type, ann_region_type);
    if (!valid_ann_region) {
        return;
    }

    // process region type parameters into AnnRegion or AnnSymbol
    std::string error_message;
    casa::AnnRegion* ann_region(nullptr);
    casa::AnnSymbol* ann_symbol(nullptr); // not a region, handle separately
    try {
        switch (ann_region_type) {
            case casa::AnnotationBase::Type::CIRCLE:
                ann_region = CreateCircleRegion(region_definition, crtf_prefix);
                break;
            case casa::AnnotationBase::Type::ELLIPSE:
                ann_region = CreateEllipseRegion(region_definition, crtf_prefix);
                break;
            case casa::AnnotationBase::Type::ROTATED_BOX: // or a CENTER_BOX if angle==0
                ann_region = CreateBoxRegion(region_definition, crtf_prefix);
                break;
            case casa::AnnotationBase::Type::POLYGON:
                ann_region = CreatePolygonRegion(region_definition, crtf_prefix);
                break;
            case casa::AnnotationBase::Type::SYMBOL: // used for carta point region
                ann_symbol = CreateSymbolRegion(region_definition, crtf_prefix);
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
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_symbol);
    } else if (ann_region != nullptr) {
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_region);
    } else {
        return;
    }
    casa::AsciiAnnotationFileLine file_line = casa::AsciiAnnotationFileLine(annotation_region);
    _region_list.addLine(file_line);
}

bool Ds9Parser::GetAnnotationRegionType(std::string& ds9_region, casa::AnnotationBase::Type& type) {
    // Convert ds9 region type (everything but "ruler") to annotation region type.
    // Returns whether conversion was successful, there is no AnnotationBase::Type::UNKNOWN!
    bool found_type(false);
    if (ds9_region == "CIRCLE") {
        type = casa::AnnotationBase::Type::CIRCLE;
        found_type = true;
    } else if (ds9_region == "ANNULUS") {
        type = casa::AnnotationBase::Type::ANNULUS;
        found_type = true;
    } else if (ds9_region == "ELLIPSE") {
        type = casa::AnnotationBase::Type::ELLIPSE;
        found_type = true;
    } else if (ds9_region == "BOX") {
        type = casa::AnnotationBase::Type::ROTATED_BOX; // or a CENTER_BOX if angle=0
        found_type = true;
    } else if (ds9_region == "POLYGON") {
        type = casa::AnnotationBase::Type::POLYGON;
        found_type = true;
    } else if (ds9_region == "LINE") {
        type = casa::AnnotationBase::Type::LINE;
        found_type = true;
    } else if (ds9_region == "TEXT") {
        type = casa::AnnotationBase::Type::TEXT;
        found_type = true;
    } else if (ds9_region == "POINT") {
        type = casa::AnnotationBase::Type::SYMBOL;
        found_type = true;
    }
    return found_type;
}

casa::AnnRegion* Ds9Parser::CreateCircleRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
    // Create AnnCircle from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    if (region_definition.size() == 4) { // circle x y radius
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
	for (size_t i = 1; i < region_definition.size(); ++i) {
            std::string param_string(region_definition[i]);
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

casa::AnnRegion* Ds9Parser::CreateEllipseRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
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
                    if (i == nparams - 1) {
                        param_quantity.setUnit("deg");
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

casa::AnnRegion* Ds9Parser::CreateBoxRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
    // Create AnnCenterBox or AnnRotBox from DS9 region definition
    casa::AnnRegion* ann_region(nullptr);
    size_t nparams(region_definition.size());
    if (nparams == 6) { // box x y width height angle
        // convert strings to Quantities
        std::vector<casacore::Quantity> parameters;
	// x and y
	for (size_t i = 1; i < nparams; ++i) {
            std::string xy_string(region_definition[i]);
            casacore::Quantity xy_quantity;
            if (readQuantity(xy_quantity, xy_string)) {
                if (xy_quantity.getUnit().empty()) {
                    if (i == nparams - 1) {
                        xy_quantity.setUnit("deg");
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

casa::AnnRegion* Ds9Parser::CreatePolygonRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
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
                    xy_quantity.setUnit("pix");
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

casa::AnnSymbol* Ds9Parser::CreateSymbolRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix) {
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
                    xy_quantity.setUnit("pix");
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
