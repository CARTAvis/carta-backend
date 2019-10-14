//# Ds9Parser.h: parse ds9 region file to get CARTA::RegionInfo

#ifndef CARTA_BACKEND__DS9PARSER_H_
#define CARTA_BACKEND__DS9PARSER_H_

#include <unordered_map>

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <imageanalysis/Annotations/AnnSymbol.h>
#include <imageanalysis/Annotations/RegionTextList.h>
#include <imageanalysis/IO/AsciiAnnotationFileLine.h>

#include <carta-protobuf/defs.pb.h>

namespace carta {

struct Ds9Properties {
    std::string text;
    std::string color = "green";
    std::string font = "helvetica 10 normal roman";
    bool select_region = true;
    bool edit_region = true;
    bool move_region = true;
    bool delete_region = true;
    bool highlite_region = true;
    bool include_region = true;
    bool fixed_region = false;
};

struct RegionProperties {
    std::string name;
    CARTA::RegionType type;
    std::vector<casacore::Quantity> control_points;
    float rotation;

    RegionProperties() {}
    RegionProperties(std::string name_, CARTA::RegionType type_, std::vector<casacore::Quantity> control_points_, float rotation_) {
        name = name_;
        type = type_;
        control_points = control_points_;
        rotation = rotation_;
    }
};

class Ds9Parser {
public:
    Ds9Parser() {}
    // constructors for import
    Ds9Parser(std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape);
    Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape);
    // constructor for export
    Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord);

    // retrieve imported regions from RegionTextList
    unsigned int NumLines(); // AsciiAnnotationFileLines stored in RegionTextList
    const casacore::Vector<casa::AsciiAnnotationFileLine> GetLines();
    casa::AsciiAnnotationFileLine LineAt(unsigned int i);
    inline std::string GetImportErrors() {
        return _import_errors;
    }

    // export regions
    void AddRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation);
    inline unsigned int NumRegions() {
        return _regions.size();
    }
    void PrintHeader(std::ostream& os);
    void PrintRegion(unsigned int i, std::ostream& os);
    void PrintRegionsToFile(std::ofstream& ofs);

private:
    void InitDs9CoordMap();

    void ProcessFileLines(std::vector<std::string>& lines);
    bool SetCoordinateSystem(std::string& ds9_coord);

    // coordinate system helpers
    bool IsDs9CoordSysKeyword(std::string& input);
    bool SetDirectionRefFrame(std::string& ds9_coord);
    void InitializeDirectionReferenceFrame(); // using input image_coord_sys

    // Import region creation
    void SetAnnotationRegion(std::string& region_description);
    bool GetAnnotationRegionType(std::string& ds9_region, casa::AnnotationBase::Type& type);
    casacore::String GetRegionName(std::string& region_properties);
    void ProcessRegionDefinition(
        casa::AnnotationBase::Type ann_region_type, std::string& region_definition, casacore::String& label, bool exclude_region);
    bool CheckAndConvertParameter(std::string& parameter, const std::string& region_type);
    casacore::String ConvertTimeFormatToDeg(std::string& parameter);
    bool ParseRegion(std::string& region_definition, std::vector<std::string>& parameters, int nparams);
    casa::AnnRegion* CreateBoxRegion(std::string& region_definition);
    casa::AnnRegion* CreateCircleRegion(std::string& region_definition);
    casa::AnnRegion* CreateEllipseRegion(std::string& region_definition);
    casa::AnnRegion* CreatePolygonRegion(std::string& region_definition);
    casa::AnnSymbol* CreateSymbolRegion(std::string& region_definition);

    // Import region errors
    void AddImportError(std::string& error);

    // region export
    void PrintBoxRegion(const RegionProperties& properties, std::ostream& os);
    void PrintEllipseRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPointRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPolygonRegion(const RegionProperties& properties, std::ostream& os);

    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _direction_ref_frame;
    bool _pixel_coord;

    // capture errors
    std::string _import_errors;

    casa::RegionTextList _region_list; // import
    std::vector<RegionProperties> _regions;
};

} // namespace carta

#endif // CARTA_BACKEND__DS9PARSER_H_
