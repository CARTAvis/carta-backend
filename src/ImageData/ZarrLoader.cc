/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"

namespace carta {

ZarrLoader::ZarrLoader(const std::string& filename) : FileLoader(filename) {}

void ZarrLoader::AllocateImage(const std::string& hdu) {
    if (_image && hdu == _hdu) {
        return;
    }

    auto image = std::make_shared<CartaZarrImage>(_filename, hdu);
    _image = image;
    _hdu = hdu;
    _image_shape = image->shape();
    _num_dims = _image_shape.size();
    _has_pixel_mask = image->hasPixelMask();
    _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(
        static_cast<casacore::CoordinateSystem*>(image->coordinates().clone()));
    _data_type = image->InternalDataType();
}

}  // namespace carta
