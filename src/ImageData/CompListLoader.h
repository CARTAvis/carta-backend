/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

class CompListLoader : public FileLoader {
public:
    CompListLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::string _filename;
    std::unique_ptr<casacore::ImageInterface<casacore::Float> > _image;
};

CompListLoader::CompListLoader(const std::string& filename) : FileLoader(filename) {}

void CompListLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        _image.reset(new casa::ComponentListImage(_filename));
        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }
        _num_dims = _image->shape().size();
    }
}

bool CompListLoader::HasData(FileInfo::Data dl) const {
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

typename CompListLoader::ImageRef CompListLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_COMPLISTLOADER_H_
