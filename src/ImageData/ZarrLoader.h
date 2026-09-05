/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

#include "FileLoader.h"

namespace carta {

class ZarrLoader : public FileLoader {
public:
    explicit ZarrLoader(const std::string& filename);

private:
    void AllocateImage(const std::string& hdu) override;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
