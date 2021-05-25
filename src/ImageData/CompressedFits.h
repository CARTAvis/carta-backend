/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_
#define CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_

#include <zlib.h>
#include <map>
#include <string>

#include <casacore/images/Images/ImageFITSConverter.h>

#include <carta-protobuf/file_info.pb.h>

// Read compressed FITS file headers

class CompressedFits {
public:
    CompressedFits(const std::string& filename) : _filename(filename) {}

    // Headers for file info
    bool GetFitsHeaderInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map);

    // File decompression
    unsigned long long GetDecompressSize();
    bool DecompressGzFile(std::string& unzip_filename);

private:
    gzFile OpenGzFile();
    bool IsImageHdu(std::string& fits_block, CARTA::FileInfoExtended& file_info_ext);
    void ParseFitsCard(casacore::String& fits_card, casacore::String& keyword, casacore::String& value, casacore::String& comment);
    void AddHeaderEntry(
        casacore::String& keyword, casacore::String& value, casacore::String& comment, CARTA::FileInfoExtended& file_info_ext);

    std::string _filename;
};

#endif // CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_
