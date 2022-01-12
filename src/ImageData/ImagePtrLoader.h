/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_

#include "FileLoader.h"

namespace carta {

class ImagePtrLoader : public FileLoader {
public:
    ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image);

    void OpenFile(const std::string& hdu) override;
};

ImagePtrLoader::ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image) : FileLoader("") {
    _image = image;

    _image_shape = _image->shape();
    _num_dims = _image_shape.size();
    _has_pixel_mask = _image->hasPixelMask();
    _coord_sys = _image->coordinates();
}

void ImagePtrLoader::OpenFile(const std::string& /*hdu*/) {}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
