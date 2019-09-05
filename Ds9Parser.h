//# Ds9Parser.h: parse ds9 region file to get CARTA::RegionInfo

#ifndef CARTA_BACKEND__DS9PARSER_H_
#define CARTA_BACKEND__DS9PARSER_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <imageanalysis/Annotations/AnnSymbol.h>
#include <imageanalysis/Annotations/RegionTextList.h>
#include <imageanalysis/IO/AsciiAnnotationFileLine.h>

#include <carta-protobuf/defs.pb.h>

namespace carta{

class Ds9Parser {
public:
    Ds9Parser() {}
    Ds9Parser(std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape);
    Ds9Parser(const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape);

    unsigned int NumLines();
    const casacore::Vector<casa::AsciiAnnotationFileLine> GetLines();
    casa::AsciiAnnotationFileLine LineAt(unsigned int i);

private:
    void ProcessFileLines(std::vector<std::string>& lines);
    bool SetCoordinateSystem(std::string& ds9_coord);
    void SetAnnotationRegion(std::string& region_description);

    // coordinate system helpers
    bool IsDs9CoordSysKeyword(std::string& input);
    bool SetDirectionRefFrame(std::string& ds9_coord);
    void InitializeDirectionReferenceFrame(); // using input image_coord_sys

    // region helpers
    void ProcessRegionDefinition(std::vector<std::string>& region_definition, std::string& crtf_prefix);
    bool GetAnnotationRegionType(std::string& ds9_region, casa::AnnotationBase::Type& type);
    casa::AnnRegion* CreateCircleRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix);
    casa::AnnRegion* CreateEllipseRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix);
    casa::AnnRegion* CreateBoxRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix);
    casa::AnnRegion* CreatePolygonRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix);
    casa::AnnSymbol* CreateSymbolRegion(std::vector<std::string>& region_definition, std::string& crtf_prefix);

    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;
    std::string _direction_ref_frame;
    bool _pixel_coords;
    casa::RegionTextList _region_list;
};

} // namespace carta

#endif // CARTA_BACKEND__DS9PARSER_H_
