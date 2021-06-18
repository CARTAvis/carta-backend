/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.h: load FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_

#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/fits/FITS/hdu.h>
#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/file_info.pb.h>
#include "../ImageData/FileLoader.h"

class FileExtInfoLoader {
public:
    FileExtInfoLoader(carta::FileLoader* loader);

    bool FillFileExtInfo(CARTA::FileInfoExtended& extended_info, const std::string& filename, const std::string& hdu, std::string& message);

private:
    // FileInfoExtended
    bool FillFileInfoFromImage(CARTA::FileInfoExtended& ext_info, const std::string& hdu, std::string& message);
    void AddMiscInfoHeaders(CARTA::FileInfoExtended& extended_info, const casacore::TableRecord& misc_info);

    // Image shape, nchannels, nstokes
    void AddShapeEntries(CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape, int chan_axis, int depth_axis,
        int stokes_axis, const std::vector<int>& render_axes);

    // Info about xy axes
    void AddComputedEntries(CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image,
        const std::vector<int>& display_axes, casacore::String& radesys, bool use_fits_header);
    void AddComputedEntriesFromHeaders(CARTA::FileInfoExtended& extended_info, const std::vector<int>& display_axes, std::string& radesys);

    // FITS keyword conversion
    bool GetFitsKwList(casacore::FitsInput& fits_input, unsigned int hdu, casacore::FitsKeywordList& kwlist);
    int GetFitsBitpix(casacore::ImageInterface<float>* image);

    // Convert MVAngle to string; returns Quantity string if not direction
    std::string MakeAngleString(const std::string& type, double val, const std::string& unit);

    // Convert Quantities and return formatted string
    std::string ConvertCoordsToDeg(const casacore::Quantity& coord0, const casacore::Quantity& coord1);
    std::string ConvertIncrementToArcsec(const casacore::Quantity& inc0, const casacore::Quantity& inc1);

    void GetCoordNames(std::string& ctype1, std::string& ctype2, std::string& radesys, std::string& coord_name1, std::string& coord_name2,
        std::string& projection);

    carta::FileLoader* _loader;
    CARTA::FileType _type;
};

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
