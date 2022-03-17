/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_

#include <casacore/casa/Json/JsonKVMap.h>
#include <casacore/casa/Json/JsonParser.h>
#include <casacore/images/Images/ImageConcat.h>

#include "FileLoader.h"

namespace carta {

template <typename T>
class ConcatLoader : public FileLoader<T> {
public:
    ConcatLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

template <typename T>
ConcatLoader<T>::ConcatLoader(const std::string& filename) : FileLoader<float>(filename) {}

template <typename T>
void ConcatLoader<T>::OpenFile(const std::string& /*hdu*/) {
    if (!this->_image) {
        casacore::JsonKVMap _jmap = casacore::JsonParser::parseFile(this->GetFileName() + "/imageconcat.json");
        this->_image.reset(new casacore::ImageConcat<float>(_jmap, this->GetFileName()));

        if (!this->_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        this->_image_shape = this->_image->shape();
        this->_num_dims = this->_image_shape.size();
        this->_has_pixel_mask = this->_image->hasPixelMask();
        this->_coord_sys = this->_image->coordinates();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_
