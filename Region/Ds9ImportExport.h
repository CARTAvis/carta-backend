//# Ds9ImportExport.h: handle DS9 region file import and export

#ifndef CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include "Region.h"

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

class Ds9ImportExport {
public:
    Ds9ImportExport() {}

    // constructors for import
    Ds9ImportExport(
        std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape, int file_id);
    Ds9ImportExport(
        const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape, int file_id);

    // constructor for export
    // Ds9ImportExport(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord);

    std::vector<RegionState> GetImportedRegions(std::string& error);

private:
    void ProcessFileLines(std::vector<std::string>& lines);

    // Coordinate system handlers
    void InitDs9CoordMap();
    bool IsDs9CoordSysKeyword(std::string& input_line);
    bool SetDirectionRefFrame(std::string& ds9_coord);
    void InitializeDirectionReferenceFrame(); // using input image_coord_sys

    // Import regions
    void SetRegion(std::string& region_description);
    void ImportPointRegion(std::string& formatted_region, std::string& name, bool exclude_region);
    void ImportCircleRegion(std::string& formatted_region, std::string& name, bool exclude_region);
    void ImportEllipseRegion(std::string& formatted_region, std::string& name, bool exclude_region);
    void ImportRectangleRegion(std::string& formatted_region, std::string& name, bool exclude_region);
    void ImportPolygonRegion(std::string& formatted_region, std::string& name, bool exclude_region);

    bool ParseRegion(std::string& region_definition, std::vector<std::string>& parameters, int nparams);
    casacore::String GetRegionName(std::string& region_properties);

    // Convert DS9 syntax -> CASA
    bool CheckAndConvertParameter(std::string& parameter, const std::string& region_type);
    casacore::String ConvertTimeFormatToDeg(std::string& parameter);

    // Convert wcs -> pixel
    bool ConvertPointToPixels(std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords);
    double AngleToLength(casacore::Quantity angle, const unsigned int pixel_axis);

    /*
    // export regions
    void AddRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation);
    inline unsigned int NumRegions() {
        return _regions.size();
    }
    void PrintHeader(std::ostream& os);
    void PrintRegion(unsigned int i, std::ostream& os);
    void PrintRegionsToFile(std::ofstream& ofs);
    // region export
    void PrintBoxRegion(const RegionProperties& properties, std::ostream& os);
    void PrintEllipseRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPointRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPolygonRegion(const RegionProperties& properties, std::ostream& os);
    */

    // Image info to import region to
    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;
    std::string _direction_ref_frame;

    // Import file coordinates are pixel or world
    bool _file_pixel_coord;

    // Ds9/CASA conversion
    std::unordered_map<std::string, std::string> _coord_map;

    // Output of import, or input to export
    std::vector<RegionState> _regions;

    // For import
    int _file_id; // to add to RegionState
    std::string _import_errors;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
