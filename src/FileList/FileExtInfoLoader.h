/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.h: load FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_

#include <map>
#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Utilities/DataType.h>
#include <casacore/fits/FITS/hdu.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/file_info.pb.h>
#include "ImageData/CompressedFits.h"
#include "ImageData/FileLoader.h"

namespace carta {

class FileExtInfoLoader {
public:
    FileExtInfoLoader(std::shared_ptr<FileLoader> loader);

    // Fill extended file info for all FITS image hdus
    bool FillFitsFileInfoMap(
        std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map, const std::string& filename, std::string& message);

    // Fill extended file info for specified hdu
    bool FillFileExtInfo(CARTA::FileInfoExtended& extended_info, const std::string& filename, const std::string& hdu, std::string& message);

private:
    bool FillFileInfoFromImage(CARTA::FileInfoExtended& ext_info, const std::string& hdu, std::string& message);
    void StripHduName(std::string& hdu); // remove extension name

    // Header entries
    casacore::Vector<casacore::String> FitsHeaderStrings(casacore::String& name, unsigned int hdu);
    void AddEntriesFromHeaderStrings(
        const casacore::Vector<casacore::String>& headers, const std::string& hdu, CARTA::FileInfoExtended& extended_info);
    void ConvertHeaderValueToNumeric(const casacore::String& name, casacore::String& value, CARTA::HeaderEntry* entry);
    void FitsHeaderInfoToHeaderEntries(casacore::ImageFITSHeaderInfo& fhi, CARTA::FileInfoExtended& extended_info);

    // Computed entries
    void AddDataTypeEntry(CARTA::FileInfoExtended& extended_info, casacore::DataType data_type);
    void AddShapeEntries(CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape, int chan_axis, int depth_axis,
        int stokes_axis, const std::vector<int>& render_axes);
    void AddInitialComputedEntries(const std::string& hdu, CARTA::FileInfoExtended& extended_info, const std::string& filename,
        const std::vector<int>& render_axes, CompressedFits* compressed_fits = nullptr);
    void AddComputedEntries(CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image,
        const std::vector<int>& display_axes, bool use_image_for_entries);
    void AddComputedEntriesFromHeaders(
        CARTA::FileInfoExtended& extended_info, const std::vector<int>& display_axes, CompressedFits* compressed_fits = nullptr);
    void AddBeamEntry(CARTA::FileInfoExtended& extended_info, const casacore::ImageBeamSet& beam_set);
    void AddCoordRanges(
        CARTA::FileInfoExtended& extended_info, const casacore::CoordinateSystem& coord_system, const casacore::IPosition& image_shape);

    // Convert MVAngle to string; returns Quantity string if not direction
    std::string MakeAngleString(const std::string& type, double val, const std::string& unit);

    // Convert Quantities and return formatted string
    std::string ConvertCoordsToDeg(const casacore::Quantity& coord0, const casacore::Quantity& coord1);
    std::string ConvertIncrementToArcsec(const casacore::Quantity& inc0, const casacore::Quantity& inc1);

    void GetCoordNames(std::string& ctype1, std::string& ctype2, std::string& radesys, std::string& coord_name1, std::string& coord_name2,
        std::string& projection);

    std::shared_ptr<FileLoader> _loader;
    CARTA::FileType _type;
};

} // namespace carta

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
