/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_

#include "FileLoader.h"

namespace carta {

class ImagePtrLoader : public FileLoader {
public:
    ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image, const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

ImagePtrLoader::ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image, const std::string& filename)
    : FileLoader(filename, "", false, true) {
    _image = image;

    _image_shape = _image->shape();
    _num_dims = _image_shape.size();
    _has_pixel_mask = _image->hasPixelMask();
    _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));
    _data_type = _image->dataType();
}

void ImagePtrLoader::OpenFile(const std::string& /*hdu*/) {}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
