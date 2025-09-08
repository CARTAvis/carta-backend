/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExport.h: get importer/exporter according to region file type

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_

#include "CrtfExport.h"
#include "CrtfImport.h"
#include "Ds9Export.h"
#include "Ds9Import.h"

namespace carta {

std::unique_ptr<RegionImport> GetRegionImporter(CARTA::FileType region_file_type,
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& region_file, bool file_is_filename) {
    // Return importer specific to the region file type
    std::unique_ptr<RegionImport> importer;
    if (region_file_type == CARTA::CRTF) {
        importer.reset(new CrtfImport(image_coord_sys, file_id, region_file, file_is_filename));
    } else if (region_file_type == CARTA::DS9_REG) {
        importer.reset(new Ds9Import(image_coord_sys, file_id, region_file, file_is_filename));
    }
    return importer;
}

std::unique_ptr<RegionExport> GetRegionExporter(CARTA::FileType region_file_type,
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis,
    bool export_pixel_coords) {
    // Return exporter specific to the region file type
    std::unique_ptr<RegionExport> exporter;
    if (region_file_type == CARTA::CRTF) {
        exporter.reset(new CrtfExport(image_coord_sys, image_shape, stokes_axis));
    } else if (region_file_type == CARTA::DS9_REG) {
        exporter.reset(new Ds9Export(image_coord_sys, image_shape, export_pixel_coords));
    }
    return exporter;
}

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_
