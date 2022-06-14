/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_FILELIST_FITSHDULIST_H_
#define CARTA_BACKEND_FILELIST_FITSHDULIST_H_

#include <string>
#include <vector>

#include <fitsio.h>

namespace carta {

class FitsHduList {
public:
    FitsHduList(const std::string& filename);
    void GetHduList(std::vector<std::string>& hdu_list, std::string& error);

private:
    void CheckFitsHeaders(fitsfile* fptr, std::vector<std::string>& hdu_list, std::string& error);

    std::string _filename;
};

} // namespace carta

#endif // CARTA_BACKEND_FILELIST_FITSHDULIST_H_
