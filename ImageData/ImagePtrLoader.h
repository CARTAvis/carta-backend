/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::shared_ptr<casacore::ImageInterface<float>> _image;
};

ImagePtrLoader::ImagePtrLoader(std::shared_ptr<casacore::ImageInterface<float>> image) : FileLoader("") {
    _image = image;
    _num_dims = _image->shape().size();
}

void ImagePtrLoader::OpenFile(const std::string& /*hdu*/) {}

bool ImagePtrLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            break;
    }
    return false;
}

typename ImagePtrLoader::ImageRef ImagePtrLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_IMAGEPTRLOADER_H_
