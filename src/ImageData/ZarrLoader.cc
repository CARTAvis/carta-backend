/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
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

    auto* zarr_image = new CartaZarrImage(_filename, hdu);
    _image.reset(zarr_image);

    _hdu = hdu;
    _image_shape = _image->shape();
    _num_dims = _image_shape.size();
    _has_pixel_mask = _image->hasPixelMask();
    _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));
    _data_type = zarr_image->InternalDataType();
}

} // namespace carta
