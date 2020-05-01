//# RegionImportExport.h: handle region import/export in CRTF and DS9 formats

#ifndef CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/defs.pb.h>

#include "Region.h"

namespace carta {

class RegionImportExport {
public:
    RegionImportExport() {}
    ~RegionImportExport() {}

    // Import constructor: file_id to add to RegionState
    RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id);
    // Export constructor
    RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape);

    // Retrieve imported regions as RegionState vector
    std::vector<RegionState> GetImportedRegions(std::string& error);

    // Add region to export: RegionState for pixel coords in reference image,
    // Quantities for world coordinates or for either coordinate type applied to another image
    virtual bool AddExportRegion(const RegionState& region) = 0;
    bool AddExportRegion(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord);

    // Perform export; ostream could be for output file (ofstream) or string (ostringstream)
    virtual bool ExportRegions(std::string& filename, std::string& error) = 0;
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

protected:
    virtual bool AddExportRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation) = 0;

    // Image info to which region is applied
    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;

    // For import
    int _file_id;
    std::string _import_errors;
    std::vector<RegionState> _import_regions;

private:
    // Return control_points and qrotation Quantity for region type
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);
    bool ConvertRecordToPolygon(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
