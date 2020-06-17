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

    // Import constructor
    // Parse input file and convert region parameters to RegionState for given image
    // file_is_filename : indicates whether file parameter contains file name or file contents.
    Ds9ImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int file_id,
        const std::string& file, bool file_is_filename);

    // Export constructor
    // Each export region will be converted to a string in DS9 format and added to string vector
    Ds9ImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coord);

    ~Ds9ImportExport();

    // Export regions
    // Convert to DS9 string and add to vector
    bool AddExportRegion(const RegionState& region) override;
    // Print regions to file or vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    bool AddExportRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation) override;

private:
    // Parse each file line and set coord sys or region
    void ProcessFileLines(std::vector<std::string>& lines);

    // Coordinate system handlers
    void InitDs9CoordMap();
    bool IsDs9CoordSysKeyword(std::string& input_line);
    bool SetFileReferenceFrame(std::string& ds9_coord);
    void SetImageReferenceFrame();

    // Import regions
    void SetRegion(std::string& region_definition);
    void ImportPointRegion(std::vector<std::string>& parameters, std::string& name, bool exclude_region);
    void ImportCircleRegion(std::vector<std::string>& parameters, std::string& name, bool exclude_region);
    void ImportEllipseRegion(std::vector<std::string>& parameters, std::string& name, bool exclude_region);
    void ImportRectangleRegion(std::vector<std::string>& parameters, std::string& name, bool exclude_region);
    void ImportPolygonRegion(std::vector<std::string>& parameters, std::string& name, bool exclude_region);

    // Convert DS9 syntax -> CASA
    bool CheckAndConvertParameter(std::string& parameter, const std::string& region_type);
    void ConvertTimeFormatToDeg(std::string& parameter);

    // Export: add header string to _export_regions
    void AddHeader();
    std::string AddExportRegionPixel(CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float angle);
    std::string AddExportRegionWorld(CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float angle);

    // DS9/CASA conversion map
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _image_ref_frame; // CASA
    std::string _file_ref_frame;  // Import: DS9 to CASA, Export: CASA to DS9

    // Whether import region file is in pixel or wcs coords
    bool _pixel_coord;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
