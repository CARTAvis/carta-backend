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

#include <casacore/casa/BasicSL/String.h>

#include <carta-protobuf/file_info.pb.h>

// Read compressed FITS file headers

namespace carta {

class CompressedFits {
public:
    CompressedFits(const std::string& filename) : _filename(filename) {}

    // Headers for file info
    bool GetFitsHeaderInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map);

    // File decompression
    unsigned long long GetDecompressSize();
    bool DecompressGzFile(std::string& unzip_file, std::string& error);

private:
    gzFile OpenGzFile();
    bool DecompressedFileExists();
    void SetDecompressFilename();

    // Extended file info
    bool IsImageHdu(std::string& fits_block, CARTA::FileInfoExtended& file_info_ext, long long& data_size);
    void ParseFitsCard(casacore::String& fits_card, casacore::String& keyword, casacore::String& value, casacore::String& comment);
    void AddHeaderEntry(
        casacore::String& keyword, casacore::String& value, casacore::String& comment, CARTA::FileInfoExtended& file_info_ext);

    std::string _filename;
    std::string _unzip_filename;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_
