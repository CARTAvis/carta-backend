/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"

#include <algorithm>

#include <casacore/casa/Exceptions/Error.h>
#include <spdlog/spdlog.h>

#include "Util/Image.h"

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

bool ZarrLoader::GetCursorSpectralData(
    std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& /*image_mutex*/) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || count_x <= 0 || count_y <= 0 || cursor_x < 0 || cursor_y < 0 || stokes < 0) {
        return false;
    }

    const auto width = _image_shape(0);
    const auto height = _image_shape(1);
    const auto depth = _image_shape(2);
    const auto num_stokes = _image_shape(3);
    if (cursor_x > width || cursor_y > height || stokes >= num_stokes || count_x > width - cursor_x || count_y > height - cursor_y) {
        return false;
    }

    const casacore::IPosition start(4, cursor_x, cursor_y, 0, stokes);
    const casacore::IPosition length(4, count_x, count_y, depth, 1);
    const casacore::Slicer section(start, length);
    data.resize(static_cast<std::size_t>(count_x) * static_cast<std::size_t>(count_y) * static_cast<std::size_t>(depth));
    casacore::Array<float> destination(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);

    try {
        // carta-zarr Image handles are immutable and safe for concurrent reads. The backend
        // mutex is intentionally not held across this I/O operation.
        image->doGetSlice(destination, section);
        return true;
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not load cursor spectral data from Zarr dataset: {}", error.getMesg());
        return false;
    }
}

bool ZarrLoader::GetChunk(std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes,
    std::mutex& /*image_mutex*/) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    data_width = 0;
    data_height = 0;
    if (!image || _num_dims != 4 || min_x < 0 || min_y < 0 || z < 0 || stokes < 0 || min_x >= _image_shape(0) || min_y >= _image_shape(1) ||
        z >= _image_shape(2) || stokes >= _image_shape(3)) {
        return false;
    }

    data_width = std::min(CHUNK_SIZE, static_cast<int>(_image_shape(0)) - min_x);
    data_height = std::min(CHUNK_SIZE, static_cast<int>(_image_shape(1)) - min_y);
    const casacore::IPosition start(4, min_x, min_y, z, stokes);
    const casacore::IPosition length(4, data_width, data_height, 1, 1);
    const casacore::Slicer section(start, length);
    data.resize(static_cast<std::size_t>(data_width) * static_cast<std::size_t>(data_height));
    casacore::Array<float> destination(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);

    try {
        image->doGetSlice(destination, section);
        return true;
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not load Zarr image chunk: {}", error.getMesg());
        data.clear();
        data_width = 0;
        data_height = 0;
        return false;
    }
}

bool ZarrLoader::UseTileCache() const {
    return true;
}

}  // namespace carta
