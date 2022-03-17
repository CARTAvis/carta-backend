/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_

#include "FileLoader.h"

namespace carta {

template <typename T>
class ImagePtrLoader : public FileLoader<float> {
public:
    ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image);

    void OpenFile(const std::string& hdu) override;
};

template <typename T>
ImagePtrLoader<T>::ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image) : FileLoader<float>("") {
    this->_image = image;
    this->_image_shape = this->_image->shape();
    this->_num_dims = this->_image_shape.size();
    this->_has_pixel_mask = this->_image->hasPixelMask();
    this->_coord_sys = this->_image->coordinates();
}

template <typename T>
void ImagePtrLoader<T>::OpenFile(const std::string& /*hdu*/) {}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
