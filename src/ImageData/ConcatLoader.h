/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

class ConcatLoader : public BaseFileLoader {
public:
    ConcatLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

ConcatLoader::ConcatLoader(const std::string& filename) : BaseFileLoader(filename) {}

void ConcatLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        casacore::JsonKVMap _jmap = casacore::JsonParser::parseFile(this->GetFileName() + "/imageconcat.json");
        _image.reset(new casacore::ImageConcat<float>(_jmap, this->GetFileName()));

        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));
        _data_type = _image->dataType();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_
