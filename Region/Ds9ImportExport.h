//# Ds9ImportExport.h: handle DS9 region file import and export

#ifndef CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include "Region.h"
#include "RegionImportExport.h"

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

class Ds9ImportExport : public RegionImportExport {
public:
    Ds9ImportExport() {}

    // Import
    Ds9ImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id,
        const std::string& file, bool file_is_filename);

    // Export
    Ds9ImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coord);

    // Export regions
    bool AddExportRegion(const RegionState& region) override;
    bool AddExportRegion(const casacore::RecordInterface& region) override;
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    void ProcessFileLines(std::vector<std::string>& lines);

    // Coordinate system handlers
    void InitDs9CoordMap();
    bool IsDs9CoordSysKeyword(std::string& input_line);
    bool SetFileReferenceFrame(std::string& ds9_coord);
    void SetImageReferenceFrame();

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

    // DS9/CASA conversion map
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _image_ref_frame; // CASA
    std::string _file_ref_frame;  // Import: DS9 to CASA, Export: CASA to DS9

    // Whether region file is in pixel or wcs coords
    bool _pixel_coord;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
