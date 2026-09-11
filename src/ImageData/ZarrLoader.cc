/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"
#include "Util/Nan.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include <casacore/casa/Exceptions/Error.h>
#include <spdlog/spdlog.h>

namespace carta {

namespace {

// CARTA asks for eleven statistics; carta-zarr accumulates the six they are all made of. The
// derivations here are the same ones Hdf5Loader performs on its own accumulators, deliberately so:
// a Zarr profile and an HDF5 profile of the same numbers have to agree to the last bit.
void StoreSpectralBlock(std::map<CARTA::StatsType, std::vector<double>>& stats, const carta::zarr::SpectralBlock& block,
    std::size_t channel_offset, double beam_area, bool has_flux) {
    const double* num_pixels = nullptr;
    const double* nan_count = nullptr;
    const double* sum = nullptr;
    const double* sum_sq = nullptr;
    const double* min = nullptr;
    const double* max = nullptr;
    for (std::size_t slot = 0; slot < block.statistic_count; ++slot) {
        const double* values = block.values + (slot * block.statistic_stride);
        switch (block.statistics[slot]) {
            case carta::zarr::Statistic::num_pixels: num_pixels = values; break;
            case carta::zarr::Statistic::nan_count: nan_count = values; break;
            case carta::zarr::Statistic::sum: sum = values; break;
            case carta::zarr::Statistic::sum_sq: sum_sq = values; break;
            case carta::zarr::Statistic::min: min = values; break;
            case carta::zarr::Statistic::max: max = values; break;
        }
    }
    if (!num_pixels || !nan_count || !sum || !sum_sq || !min || !max) {
        return;
    }

    for (std::size_t c = 0; c < static_cast<std::size_t>(block.channel_count); ++c) {
        const auto z = channel_offset + static_cast<std::size_t>(block.first_channel) + c;
        const double n = num_pixels[c];
        stats[CARTA::StatsType::NumPixels][z] = n;
        stats[CARTA::StatsType::NanCount][z] = nan_count[c];
        if (n == 0.0) {
            // Everything but the two counts is undefined for a channel with no valid pixel, and the
            // profile is already NaN there.
            continue;
        }
        stats[CARTA::StatsType::Sum][z] = sum[c];
        stats[CARTA::StatsType::SumSq][z] = sum_sq[c];
        stats[CARTA::StatsType::Min][z] = min[c];
        stats[CARTA::StatsType::Max][z] = max[c];
        stats[CARTA::StatsType::Mean][z] = sum[c] / n;
        stats[CARTA::StatsType::RMS][z] = std::sqrt(sum_sq[c] / n);
        stats[CARTA::StatsType::Sigma][z] = n > 1.0 ? std::sqrt((sum_sq[c] - (sum[c] * sum[c] / n)) / (n - 1.0)) : 0.0;
        stats[CARTA::StatsType::Extrema][z] = std::abs(min[c]) > std::abs(max[c]) ? min[c] : max[c];
        if (has_flux) {
            stats[CARTA::StatsType::FluxDensity][z] = sum[c] / beam_area;
        }
    }
}

}  // namespace

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

// Zarr has one copy of the pixels, so there is no second layout to choose between and no
// crossover to find: whatever the region's shape, this loader reads exactly the chunks it covers
// and accumulates in one pass, where casacore's route iterates cursors and asks for the mask
// separately. The HDF5 heuristic this replaces (height * depth < width) exists because HDF5 really
// does have two datasets on disk and picking the wrong one is expensive.
bool ZarrLoader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& /*image_mutex*/) {
    return std::dynamic_pointer_cast<CartaZarrImage>(_image) != nullptr && _num_dims == 4 && region_shape.size() >= 2;
}

// One region's spectral profile, resumed where the last call left off.
//
// The caller loops until progress reaches 1, checking between calls whether the region still
// exists and whether the user still wants the answer, so each call does a bounded amount of work
// and returns. The pause happens at a block boundary, and blocks are aligned to whole spectral
// chunks, so resuming decodes nothing twice.
//
// Channels are finished in order rather than all of them being refined together: a channel this
// call reports is final, and the ones after it stay NaN until their turn.
bool ZarrLoader::GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& /*image_mutex*/,
    std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3)) {
        return false;
    }
    const auto mask_shape = mask.shape();
    if (mask_shape.size() != 2 || origin.size() < 2 || !mask.asArray().contiguousStorage()) {
        return false;
    }

    const int depth = _image_shape(2);
    AxisRange range(z_range.from, z_range.to == ALL_Z ? depth - 1 : z_range.to);
    if (range.from < 0 || range.to < range.from || range.to >= depth) {
        return false;
    }
    const auto channels = static_cast<std::size_t>(range.to - range.from + 1);

    const double beam_area = CalculateBeamArea();
    const bool has_flux = !std::isnan(beam_area);

    std::scoped_lock lock(_region_spectral_mutex);
    auto& state = _region_spectral[{region_id, stokes}];
    // Compared size first: casacore's IPosition comparison throws rather than returning false when
    // the two do not conform, and the stored one is empty until the first call.
    const bool same_region = state.origin.size() == origin.size() && state.shape.size() == mask_shape.size() &&
                             state.origin == origin && state.shape == mask_shape;
    if (!same_region || state.z_range.from != range.from || state.z_range.to != range.to) {
        // A moved or resized region is a different question, not a continuation of this one.
        state.origin = origin;
        state.shape = mask_shape;
        state.z_range = range;
        state.channels_done = 0;
        state.stats.clear();
        const std::vector<CARTA::StatsType> reported{CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount,
            CARTA::StatsType::Sum, CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma,
            CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max, CARTA::StatsType::Extrema};
        for (const auto stat : reported) {
            state.stats[stat] = std::vector<double>(channels, DOUBLE_NAN);
        }
        if (has_flux) {
            state.stats[CARTA::StatsType::FluxDensity] = std::vector<double>(channels, DOUBLE_NAN);
        }
    }

    if (state.channels_done < channels) {
        static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to borrow a mask");
        const carta::zarr::RegionMask region{static_cast<std::uint64_t>(origin(0)), static_cast<std::uint64_t>(origin(1)),
            static_cast<std::uint64_t>(mask_shape(0)), static_cast<std::uint64_t>(mask_shape(1)),
            reinterpret_cast<const std::uint8_t*>(mask.asArray().data())};

        carta::zarr::SpectralReduceRequest request;
        request.spectral = {static_cast<std::uint64_t>(range.from + state.channels_done),
            static_cast<std::uint64_t>(channels - state.channels_done), 1};
        request.polarization = static_cast<std::uint64_t>(stokes);
        request.regions = &region;
        request.region_count = 1;
        request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::nan_count |
                             carta::zarr::Statistic::sum | carta::zarr::Statistic::sum_sq |
                             carta::zarr::Statistic::min | carta::zarr::Statistic::max;

        const auto first_channel = state.channels_done;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TARGET_PARTIAL_REGION_TIME);
        bool paused = false;
        try {
            const bool finished = image->ReduceSpectral(request, [&](const carta::zarr::SpectralBlock& block) {
                StoreSpectralBlock(state.stats, block, first_channel, beam_area, has_flux);
                state.channels_done = first_channel + static_cast<std::size_t>(block.first_channel + block.channel_count);
                if (std::chrono::steady_clock::now() >= deadline && state.channels_done < channels) {
                    // Hand back what is finished so the caller can show it and decide whether this
                    // profile is still wanted.
                    paused = true;
                    return false;
                }
                return true;
            });
            if (!finished && !paused) {
                return false;
            }
        } catch (const casacore::AipsError& error) {
            spdlog::warn("Could not reduce a region over the spectrum of a Zarr dataset: {}", error.getMesg());
            return false;
        }
    }

    results = state.stats;
    progress = float(state.channels_done) / float(channels);
    return true;
}

}  // namespace carta
