/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_FITSLOADER_H_
#define CARTA_SRC_IMAGEDATA_FITSLOADER_H_

#include <fitsio.h>

#include "FileLoader.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& filename, bool is_gz = false, bool is_http = false);
    ~FitsLoader();

private:
    std::string _unzip_file;
    casacore::uInt _hdu_num;
    bool _is_http;

    void AllocateImage(const std::string& hdu) override;
    int GetNumImageHeaders(unsigned int hdu_num, std::string& error);

    // Image beam headers/table - casacore workarounds
    void ResetImageBeam(unsigned int hdu_num);
    bool HasBeamsTable();
    bool HasBeamHeaders(unsigned int hdu_num);
    bool GetLastHistoryBeam(unsigned int hdu_num, casacore::Quantity& major, casacore::Quantity& minor, casacore::Quantity& pa);
    bool Is64BitBeamsTable();

    bool OpenHdu(unsigned int hdu_num, fitsfile*& fptr, int& hdu_type);
    bool OpenBeamsTable(fitsfile*& fptr);
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_FITSLOADER_H_
