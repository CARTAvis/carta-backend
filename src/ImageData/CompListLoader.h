/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_COMPLISTLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_COMPLISTLOADER_H_

#include <casacore/casa/Json/JsonKVMap.h>
#include <casacore/casa/Json/JsonParser.h>
#include <imageanalysis/Images/ComponentListImage.h>

#include "FileLoader.h"

namespace carta {

template <typename T>
class CompListLoader : public FileLoader<T> {
public:
    CompListLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

template <typename T>
CompListLoader<T>::CompListLoader(const std::string& filename) : FileLoader<float>(filename) {}

template <typename T>
void CompListLoader<T>::OpenFile(const std::string& /*hdu*/) {
    if (!this->_image) {
        this->_image.reset(new casa::ComponentListImage(this->_filename));

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

#endif // CARTA_BACKEND_IMAGEDATA_COMPLISTLOADER_H_
