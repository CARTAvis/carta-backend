/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_COMPLISTLOADER_H_
#define CARTA_SRC_IMAGEDATA_COMPLISTLOADER_H_

#include <casacore/casa/Json/JsonKVMap.h>
#include <casacore/casa/Json/JsonParser.h>
#include <imageanalysis/Images/ComponentListImage.h>

#include "FileLoader.h"

namespace carta {

class CompListLoader : public FileLoader {
public:
    CompListLoader(const std::string& filename);

private:
    void AllocateImage(const std::string& hdu) override;
};

CompListLoader::CompListLoader(const std::string& filename) : FileLoader(filename) {}

void CompListLoader::AllocateImage(const std::string& /*hdu*/) {
    if (!_image) {
        _image.reset(new casa::ComponentListImage(_filename));

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

#endif // CARTA_SRC_IMAGEDATA_COMPLISTLOADER_H_
