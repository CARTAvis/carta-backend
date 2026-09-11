/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"

#include <cstdint>

#include <casacore/casa/Exceptions/Error.h>
#include <spdlog/spdlog.h>

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
    std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& /*image_mutex*/,
    const std::function<bool()>& cancellation_requested) {
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
    carta::zarr::ReadOptions options;
    options.cancellation_requested = cancellation_requested;

    try {
        // carta-zarr Image handles are immutable and safe for concurrent reads. The backend
        // mutex is intentionally not held across this I/O operation.
        image->Read(destination, section, options);
        return true;
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not load cursor spectral data from Zarr dataset: {}", error.getMesg());
        data.clear();
        return false;
    }
}

}  // namespace carta
