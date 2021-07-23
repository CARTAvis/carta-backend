/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CASALOADER_H_

#include <casacore/images/Images/PagedImage.h>

#include "FileLoader.h"

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

CasaLoader::CasaLoader(const std::string& filename) : FileLoader(filename) {}

void CasaLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        _image.reset(new casacore::PagedImage<float>(_filename));

        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = _image->coordinates();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
