/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExport.h: get importer/exporter according to region file type

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_

#include "CrtfExporter.h"
#include "CrtfImporter.h"
#include "Ds9Exporter.h"
#include "Ds9Importer.h"

namespace carta {

/**
 * @brief Create and return importer according to region file type.
 * @param region_file_type CARTA FileType enum designating CRTF or DS9 region file
 * @param image_coord_sys casacore::CoordinateSystem of image to which region is imported
 * @param file_id File id of reference image to which regions will be imported
 * @param region_file File name or contents to import
 * @param file_is_filename Indicates whether file parameter contains name or contents.
 * @return region file importer, either CrtfImporter or Ds9Importer
 */
std::unique_ptr<RegionImporter> GetRegionImporter(CARTA::FileType region_file_type,
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& region_file, bool file_is_filename) {
    // Return importer specific to the region file type
    std::unique_ptr<RegionImporter> importer;
    if (region_file_type == CARTA::CRTF) {
        importer.reset(new CrtfImporter(image_coord_sys, file_id, region_file, file_is_filename));
    } else if (region_file_type == CARTA::DS9_REG) {
        importer.reset(new Ds9Importer(image_coord_sys, file_id, region_file, file_is_filename));
    }
    return importer;
}

/**
 * @brief Create and return exporter according to region file type.
 * @param region_file_type CARTA FileType enum designating CRTF or DS9 region file
 * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
 * @param image_shape casacore::IPosition describing shape of image from which region is exported
 * @param stokes_axis Current stokes axis index in image from which region is exported
 * @param export_pixel_coords Whether to export in pixel or world coordinates.
 * @return region exporter, either CrtfExporter or Ds9Exporter
 */
std::unique_ptr<RegionExporter> GetRegionExporter(CARTA::FileType region_file_type,
    std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis,
    bool export_pixel_coords) {
    // Return exporter specific to the region file type
    std::unique_ptr<RegionExporter> exporter;
    if (region_file_type == CARTA::CRTF) {
        exporter.reset(new CrtfExporter(image_coord_sys, image_shape, stokes_axis));
    } else if (region_file_type == CARTA::DS9_REG) {
        exporter.reset(new Ds9Exporter(image_coord_sys, image_shape, export_pixel_coords));
    }
    return exporter;
}

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORT_H_
