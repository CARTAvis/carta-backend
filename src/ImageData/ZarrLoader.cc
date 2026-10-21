/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <spdlog/spdlog.h>

#include "CartaZarrImage.h"
#include "Util/Image.h"
#include "Util/Nan.h"
#include "ZarrContext.h"

namespace carta {
namespace {

// The cache pool for a spectrum read. A spectrum reads a column of chunks through the whole depth, in
// order. If the column fits in half of the shared cache, the default pool is used: the chunks stay cached,
// so reading the same spectrum again is fast. Otherwise a pool that keeps nothing is used: a column larger
// than the cache would evict its own chunks before they are reused, and one that fills much of it would
// push out the chunks of the plane on show. With no shared cache there is nothing to protect.
std::optional<carta::zarr::CachePool> PoolForSpectrum(const CartaZarrImage& image, std::int64_t x, std::int64_t y, std::int64_t width,
    std::int64_t height, std::int64_t z, std::int64_t depth) {
    const auto cache_bytes = ZarrCacheBytes();
    if (cache_bytes == 0) {
        return std::nullopt;
    }
    const auto chunk = image.ChunkShape();
    // The chunks along an axis that [from, from + count) touches
    const auto touched = [](std::int64_t from, std::int64_t count, std::int64_t unit) {
        return static_cast<std::uint64_t>((from + count - 1) / unit - from / unit + 1);
    };
    const std::uint64_t chunks = touched(x, width, chunk(0)) * touched(y, height, chunk(1)) * touched(z, depth, chunk(2));
    if (chunks * image.ZarrImage().DecodedChunkBytes() <= cache_bytes / 2) {
        return std::nullopt;
    }
    auto pool = GetZarrContext().CreateCachePool(0);
    if (!pool) {
        spdlog::warn("A spectrum reads through the session's cache: {}", pool.error().message);
        return std::nullopt;
    }
    return *pool;
}

} // namespace

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
    _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(image->coordinates().clone()));
    _data_type = image->InternalDataType();
}

bool ZarrLoader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
    std::mutex& /*image_mutex*/, const SpectralReadProgress& progress) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    // One pixel only: the HDF5 loader lays out several pixels' spectra channel fastest, and a read in CARTA's
    // order would not be. carta-zarr's finished prefix is then the first channels, which progress counts.
    if (!image || _num_dims != 4 || count_x != 1 || count_y != 1 || cursor_x < 0 || cursor_y < 0 || stokes < 0 ||
        cursor_x >= _image_shape(0) || cursor_y >= _image_shape(1) || stokes >= _image_shape(3)) {
        return false;
    }
    const auto depth = _image_shape(2);

    const casacore::Slicer section(casacore::IPosition(4, cursor_x, cursor_y, 0, stokes), casacore::IPosition(4, 1, 1, depth, 1));
    // NaN rather than zero for the channels not read yet: the caller sends this buffer before it is full
    data.assign(static_cast<std::size_t>(depth), FLOAT_NAN);
    casacore::Array<float> destination(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);

    carta::zarr::ReadOptions options;
    options.control.cache_pool = PoolForSpectrum(*image, cursor_x, cursor_y, 1, 1, 0, depth);

    // carta-zarr images are safe to read from several threads, so the image mutex is not held for the read
    const auto read = image->Read(destination, section, options, progress);
    if (!read) {
        if (read.error().code != carta::zarr::ErrorCode::cancelled) {
            spdlog::warn("Could not read the cursor spectrum of a Zarr image: {}", read.error().message);
        }
        data.clear();
        return false;
    }
    return true;
}

// Zarr keeps one copy of the pixels, so whatever the region's shape this reads only the chunks it covers,
// in one pass. The HDF5 loader chooses because it has two copies, and the wrong one is slow.
bool ZarrLoader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& /*image_mutex*/) {
    return std::dynamic_pointer_cast<CartaZarrImage>(_image) != nullptr && _num_dims == 4 && region_shape.size() >= 2;
}

namespace {

// A 64-bit FNV-1a hash of a mask's pixels, and how many are in the region
std::pair<std::uint64_t, std::size_t> MaskFingerprint(const casacore::Array<casacore::Bool>& mask) {
    bool delete_storage(false);
    const casacore::Bool* pixels = mask.getStorage(delete_storage);
    std::uint64_t hash = 14695981039346656037ULL;
    std::size_t count = 0;
    for (std::size_t i = 0; i < mask.nelements(); ++i) {
        hash = (hash ^ static_cast<std::uint64_t>(pixels[i] ? 1 : 0)) * 1099511628211ULL;
        count += pixels[i] ? 1 : 0;
    }
    mask.freeStorage(pixels, delete_storage);
    return {hash, count};
}

} // namespace

ZarrLoader::RegionSpectralKey ZarrLoader::RegionSpectralKey::Of(
    const casacore::IPosition& origin, const casacore::Array<casacore::Bool>& mask, const AxisRange& range) {
    RegionSpectralKey key;
    key.x = origin(0);
    key.y = origin(1);
    key.width = mask.shape()(0);
    key.height = mask.shape()(1);
    // A region edited within the same bounding box has the same origin and shape, and another mask
    std::tie(key.mask_hash, key.mask_pixels) = MaskFingerprint(mask);
    key.z_from = range.from;
    key.z_to = range.to;
    return key;
}

bool ZarrLoader::RegionSpectralKey::operator==(const RegionSpectralKey& other) const {
    return x == other.x && y == other.y && width == other.width && height == other.height && mask_hash == other.mask_hash &&
           mask_pixels == other.mask_pixels && z_from == other.z_from && z_to == other.z_to;
}

// Reads a region's profile in runs of whole channels, in channel order. Each reported channel is final, and
// the channels not read yet are NaN. A call stops at the end of a run once it has read for
// _region_spectral_time, and the next call continues from there, so no chunk is read twice.
bool ZarrLoader::GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& /*image_mutex*/,
    std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3)) {
        return false;
    }
    const auto& mask_array = mask.asArray();
    const auto mask_shape = mask_array.shape();
    if (mask_shape.size() != 2 || origin.size() < 2 || !mask_array.contiguousStorage()) {
        return false;
    }
    const int depth = _image_shape(2);
    const AxisRange range(z_range.from, z_range.to == ALL_Z ? depth - 1 : z_range.to);
    if (range.from < 0 || range.to < range.from || range.to >= depth) {
        return false;
    }
    const auto channels = static_cast<std::size_t>(range.to - range.from + 1);
    const double beam_area = CalculateBeamArea();
    const bool has_flux = !std::isnan(beam_area);

    const auto key = RegionSpectralKey::Of(origin, mask_array, range);
    std::scoped_lock lock(_region_spectral_mutex);
    auto& pace = _region_spectral[region_id];
    if (!(pace.key == key)) {
        // Every stokes of the region starts again. One kept from an earlier read of the same region may already
        // be finished, and would then report a computed stokes as finished while the others are not.
        pace = RegionSpectralPace{key};
    }
    auto [found, added] = pace.stokes.try_emplace(stokes);
    auto& state = found->second;
    if (added) {
        for (const auto stat : {CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount, CARTA::StatsType::Sum, CARTA::StatsType::Mean,
                 CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max,
                 CARTA::StatsType::Extrema}) {
            state.stats[stat] = std::vector<double>(channels, DOUBLE_NAN);
        }
        if (has_flux) {
            state.stats[CARTA::StatsType::FluxDensity] = std::vector<double>(channels, DOUBLE_NAN);
        }
    }
    const auto thread = std::this_thread::get_id();
    const auto previous_read = state.reads.count(thread) ? state.reads[thread].read_index : -1;

    if (state.channels_done < channels) {
        // If another stokes of this region is further along, read only up to where it got, so that this stokes
        // catches up without getting ahead. Either way stop after _region_spectral_time: without that limit, a
        // stokes catching up with one that already finished would read to the end in one call.
        const auto last = state.channels_done < pace.channels_done ? pace.channels_done : channels;

        static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to lend a mask");
        const carta::zarr::RegionMask region{static_cast<std::uint64_t>(origin(0)), static_cast<std::uint64_t>(origin(1)),
            static_cast<std::uint64_t>(mask_shape(0)), static_cast<std::uint64_t>(mask_shape(1)),
            {reinterpret_cast<const std::uint8_t*>(mask_array.data()), static_cast<std::size_t>(mask_array.nelements())}};

        const auto first = state.channels_done;
        carta::zarr::SpectralReduceRequest request;
        request.planes.spectral = {static_cast<std::uint64_t>(range.from) + first, static_cast<std::uint64_t>(last - first), 1};
        request.planes.polarization = static_cast<std::uint64_t>(stokes);
        request.regions = {&region, 1};
        request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::nan_count | carta::zarr::Statistic::sum |
                             carta::zarr::Statistic::sum_sq | carta::zarr::Statistic::min | carta::zarr::Statistic::max |
                             carta::zarr::Statistic::sum_sq_dev;
        request.emit_every_channels = _region_spectral_channels_per_emit;

        carta::zarr::ReadOptions options;
        // Counted over the whole range rather than what is left of it, so that a profile read over several
        // calls keeps to one kind of pool
        options.control.cache_pool = PoolForSpectrum(*image, origin(0), origin(1), mask_shape(0), mask_shape(1), range.from, channels);
        const auto deadline = std::chrono::steady_clock::now() + _region_spectral_time;
        bool paused(false);
        // Looked up once rather than for every channel
        auto& num_pixels = state.stats[CARTA::StatsType::NumPixels];
        auto& nan_count = state.stats[CARTA::StatsType::NanCount];
        auto& sum = state.stats[CARTA::StatsType::Sum];
        auto& sum_sq = state.stats[CARTA::StatsType::SumSq];
        auto& min = state.stats[CARTA::StatsType::Min];
        auto& max = state.stats[CARTA::StatsType::Max];
        auto& mean = state.stats[CARTA::StatsType::Mean];
        auto& rms = state.stats[CARTA::StatsType::RMS];
        auto& sigma = state.stats[CARTA::StatsType::Sigma];
        auto& extrema = state.stats[CARTA::StatsType::Extrema];
        auto* flux_density = has_flux ? &state.stats[CARTA::StatsType::FluxDensity] : nullptr;
        const auto store = [&](const carta::zarr::SpectralBlock& block) {
            if (!block.complete) {
                // Partial sums of a run: the same channels come again, complete
                return true;
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                const auto z = first + static_cast<std::size_t>(block.first_channel + c);
                const auto counted = block.Totals(0, c);
                const double n = counted.num_pixels;
                num_pixels[z] = n;
                nan_count[z] = counted.nan_count;
                if (n == 0.0) {
                    // Every other statistic of a channel with no valid pixel is NaN, as the HDF5 loader reports
                    continue;
                }
                // As the HDF5 loader reports them, but for the spread
                sum[z] = counted.sum;
                sum_sq[z] = counted.sum_sq;
                min[z] = counted.min;
                max[z] = counted.max;
                mean[z] = counted.sum / n;
                rms[z] = std::sqrt(counted.sum_sq / n);
                // The spread from the centered moment rather than from sum_sq - sum^2 / n, which loses a small
                // spread about a large mean
                sigma[z] = n > 1.0 ? std::sqrt(counted.sum_sq_dev / (n - 1.0)) : 0.0;
                extrema[z] = std::abs(counted.min) > std::abs(counted.max) ? counted.min : counted.max;
                if (flux_density) {
                    (*flux_density)[z] = counted.sum / beam_area;
                }
            }
            state.channels_done = first + static_cast<std::size_t>(block.first_channel + block.channel_count);
            if (state.channels_done < last && std::chrono::steady_clock::now() >= deadline) {
                // Hand back what is finished, so the caller can send it and decide whether the profile is still wanted
                paused = true;
                return false;
            }
            return true;
        };
        const auto reduced = image->ZarrImage().ReduceSpectral(request, store, options);
        // Stopping at the deadline is this loader's own choice; carta-zarr reports it as cancelled, which is
        // not an error
        if (!reduced && !paused) {
            spdlog::warn("Could not reduce a region over the spectrum of a Zarr image: {}", reduced.error().message);
            return false;
        }
        pace.channels_done = std::max(pace.channels_done, state.channels_done);
    }

    results = state.stats;
    state.reads[thread] = {++pace.reads, state.channels_done};
    auto done = state.channels_done;
    for (const auto& [other_stokes, other] : pace.stokes) {
        const auto read = other.reads.find(thread);
        if (read != other.reads.end() && read->second.read_index > previous_read) {
            done = std::min(done, read->second.channels_done);
        }
    }
    progress = static_cast<float>(done) / static_cast<float>(channels);
    return true;
}

} // namespace carta
