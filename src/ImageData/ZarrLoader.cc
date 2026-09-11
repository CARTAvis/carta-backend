/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"

#include <cstdint>
#include <vector>

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

// The batched path exists for the position-velocity generator, which asks for one small box per
// pixel along a line. carta-zarr walks the chunks once and accumulates whichever boxes land on
// each, so the cost follows the chunks the boxes cover rather than the number of boxes.
bool ZarrLoader::GetMultiRegionSpectralData(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
    const std::function<bool(const RegionSpectralBlock&)>& sink) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || regions.empty() || !sink || stokes < 0) {
        return false;
    }

    const auto depth = _image_shape(2);
    const auto num_stokes = _image_shape(3);
    if (stokes >= num_stokes || z_range.from < 0 || z_range.to < z_range.from || z_range.to >= depth) {
        return false;
    }

    // casacore stores a Bool as one byte and an LCRegionFixed's mask contiguously with x fastest,
    // which is what carta-zarr's RegionMask already asks for, so the masks are handed over as they
    // are rather than converted.
    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to borrow a mask");
    std::vector<carta::zarr::RegionMask> zarr_regions;
    zarr_regions.reserve(regions.size());
    for (const auto& region : regions) {
        zarr_regions.push_back({region.x_start, region.y_start, region.width, region.height,
            reinterpret_cast<const std::uint8_t*>(region.mask)});
    }

    carta::zarr::SpectralReduceRequest request;
    request.spectral = {static_cast<std::uint64_t>(z_range.from), static_cast<std::uint64_t>(z_range.to - z_range.from + 1), 1};
    request.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = zarr_regions.data();
    request.region_count = zarr_regions.size();
    // The generator wants a mean, and a mean is a sum over a count. Nothing else is read, so
    // nothing else is accumulated.
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum;

    try {
        return image->ReduceSpectral(request, [&](const carta::zarr::SpectralBlock& block) {
            RegionSpectralBlock forwarded;
            forwarded.first_channel = static_cast<std::size_t>(block.first_channel);
            forwarded.channel_count = static_cast<std::size_t>(block.channel_count);
            forwarded.region_count = zarr_regions.size();
            forwarded.region_stride = block.region_stride;
            // Both statistics live in the one buffer; their slots are what the block declares.
            for (std::size_t slot = 0; slot < block.statistic_count; ++slot) {
                const double* values = block.values + (slot * block.statistic_stride);
                if (block.statistics[slot] == carta::zarr::Statistic::num_pixels) {
                    forwarded.num_pixels = values;
                } else if (block.statistics[slot] == carta::zarr::Statistic::sum) {
                    forwarded.sum = values;
                }
            }
            if (forwarded.num_pixels == nullptr || forwarded.sum == nullptr) {
                return false;
            }
            return sink(forwarded);
        });
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not reduce regions over the spectrum of a Zarr dataset: {}", error.getMesg());
        return false;
    }
}

}  // namespace carta
