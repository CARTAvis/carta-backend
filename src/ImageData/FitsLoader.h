/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_FITSLOADER_H_
#define CARTA_SRC_IMAGEDATA_FITSLOADER_H_

#include "FileLoader.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& filename, bool is_gz = false);
    ~FitsLoader();

private:
    std::string _unzip_file;
    casacore::uInt _hdu_num;

    void AllocateImage(const std::string& hdu) override;
    int GetNumHeaders(const std::string& filename, int hdu);

    // Image beam headers/table
    bool Is64BitBeamsTable(const std::string& filename);
    void ResetImageBeam(unsigned int hdu_num);
    bool HasBeamHeaders(unsigned int hdu_num);
    bool GetLastHistoryBeam(unsigned int hdu_num, casacore::Quantity& major, casacore::Quantity& minor, casacore::Quantity& pa);
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_FITSLOADER_H_
