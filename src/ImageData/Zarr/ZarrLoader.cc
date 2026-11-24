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
#include <limits>
#include <memory>
#include <optional>

#include <spdlog/spdlog.h>

#include "CartaZarrImage.h"
#include "Util/Image.h"
#include "Util/Nan.h"
#include "ZarrContext.h"

namespace carta {
namespace {

// A cache pool that keeps nothing, so a read does not evict chunks from the shared cache.
// Returns nothing if there is no shared cache, or if the pool cannot be made. Then the read uses the default pool.
std::optional<carta::zarr::CachePool> PoolThatKeepsNothing() {
    if (ZarrCacheBytes() == 0) {
        return std::nullopt;
    }
    auto pool = GetZarrContext().CreateCachePool(0);
    if (!pool) {
        spdlog::warn("A spectrum reads through the session's cache: {}", pool.error().message);
        return std::nullopt;
    }
    return *pool;
}

// The cache pool for a spectrum read. If its column of chunks fits in the shared cache, the default pool keeps
// the chunks for the next pixel. Otherwise a pool that keeps nothing is used, because the column would evict its
// own chunks.
std::optional<carta::zarr::CachePool> PoolForSpectrum(const CartaZarrImage& image, std::int64_t x, std::int64_t y, std::int64_t width,
    std::int64_t height, std::int64_t z, std::int64_t depth) {
    const auto cache_bytes = ZarrCacheBytes();
    if (cache_bytes == 0) {
        return std::nullopt;
    }
    const auto chunk = image.ChunkShape();
    // The number of chunks along an axis that [from, from + count) touches
    const auto touched = [](std::int64_t from, std::int64_t count, std::int64_t unit) {
        return static_cast<std::uint64_t>((from + count - 1) / unit - from / unit + 1);
    };
    if (SpectrumKeepsItsChunks(touched(x, width, chunk(0)), touched(y, height, chunk(1)), touched(z, depth, chunk(2)),
            image.ZarrImage().DecodedChunkBytes(), cache_bytes)) {
        return std::nullopt;
    }
    return PoolThatKeepsNothing();
}

// The statistics that CalcBasicStats gives for the same pixels. Sigma comes from the centered moment instead
// of sum_sq - sum^2 / n, which loses a small spread about a large mean.
BasicStats<float> ToBasicStats(const carta::zarr::SpectralTotals& counted) {
    const auto count = static_cast<std::size_t>(counted.num_pixels);
    if (count == 0) {
        return BasicStats<float>{
            0, 0.0, DOUBLE_NAN, DOUBLE_NAN, std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), DOUBLE_NAN, 0.0};
    }
    const double n = counted.num_pixels;
    const double sigma = count > 1 ? std::sqrt(counted.sum_sq_dev / (n - 1.0)) : DOUBLE_NAN;
    return BasicStats<float>{count, counted.sum, counted.sum / n, sigma, static_cast<float>(counted.min), static_cast<float>(counted.max),
        std::sqrt(counted.sum_sq / n), counted.sum_sq};
}

// What a scan's result means to the caller. A failure is logged, because the caller only sees that it failed.
template <typename T>
FileLoader::LoaderScan ScanOutcome(const carta::zarr::Result<T>& result, const char* what) {
    if (result) {
        return FileLoader::LoaderScan::finished;
    }
    if (result.error().code == carta::zarr::ErrorCode::cancelled) {
        return FileLoader::LoaderScan::cancelled;
    }
    spdlog::warn("Could not {} of a Zarr image: {}", what, result.error().message);
    return FileLoader::LoaderScan::failed;
}

} // namespace

bool SpectrumKeepsItsChunks(
    std::uint64_t chunks_x, std::uint64_t chunks_y, std::uint64_t chunks_z, std::uint64_t chunk_bytes, std::size_t cache_bytes) {
    // A chunk count that overflows 64 bits does not fit.
    std::uint64_t chunks = 1;
    for (const auto count : {chunks_x, chunks_y, chunks_z}) {
        if (count != 0 && chunks > std::numeric_limits<std::uint64_t>::max() / count) {
            return false;
        }
        chunks *= count;
    }
    // Divide, because chunks * chunk_bytes can overflow.
    return chunk_bytes == 0 || chunks <= cache_bytes / chunk_bytes;
}

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
    // One pixel only. For several pixels, the HDF5 loader puts the channel axis fastest.
    if (!image || _num_dims != 4 || count_x != 1 || count_y != 1 || cursor_x < 0 || cursor_y < 0 || stokes < 0 ||
        cursor_x >= _image_shape(0) || cursor_y >= _image_shape(1) || stokes >= _image_shape(3)) {
        return false;
    }
    const auto depth = _image_shape(2);

    const casacore::Slicer section(casacore::IPosition(4, cursor_x, cursor_y, 0, stokes), casacore::IPosition(4, 1, 1, depth, 1));
    // NaN, not zero, for the channels not read yet. The caller sends the buffer before it is full.
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

// True for a 4-D Zarr image and a region of at least two axes.
// The HDF5 loader chooses between two copies of the pixels, but a Zarr store has only one.
bool ZarrLoader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& /*image_mutex*/) {
    return std::dynamic_pointer_cast<CartaZarrImage>(_image) != nullptr && _num_dims == 4 && region_shape.size() >= 2;
}

bool ZarrLoader::RegionSpectralPace::IsOf(const RegionSpectralKey& other_key, const casacore::Array<casacore::Bool>& other_mask) const {
    // The key includes the mask shape. Both masks are contiguous, as GetRegionSpectralData checks.
    return key == other_key && mask.shape().isEqual(other_mask.shape()) &&
           std::equal(mask.data(), mask.data() + mask.nelements(), other_mask.data());
}

void ZarrLoader::RegionSpectralPace::Reset(const RegionSpectralKey& other_key, const casacore::Array<casacore::Bool>& other_mask) {
    key = other_key;
    mask.reference(other_mask);
    channels_done = 0;
    reads = 0;
    stokes.clear();
}

// Reads a region's profile in runs of whole channels. A call returns after _region_spectral_time,
// and the next call continues from there. The channels not read yet are NaN.
bool ZarrLoader::GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& /*image_mutex*/,
    std::map<CARTA::StatsType, std::vector<double>>& results, float& progress, const SpectralReadWanted& still_wanted) {
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

    const RegionSpectralKey key(origin, mask_shape, range);
    auto& pace = [&]() -> RegionSpectralPace& {
        std::scoped_lock regions_lock(_region_spectral_paces_mutex);
        return _region_spectral_paces[region_id];
    }();
    std::scoped_lock lock(pace.mutex);
    if (!pace.IsOf(key, mask_array)) {
        // Start every stokes again. Otherwise a stokes finished for an earlier key would report a computed stokes as finished.
        pace.Reset(key, mask_array);
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
        // A stokes behind another stokes of the region reads only up to it. It still stops after _region_spectral_time.
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
        // The chunks are not kept. The finished channels are kept in the pace, so the chunks are rarely read again.
        // Keeping them would evict the chunks of the displayed plane.
        options.control.cache_pool = PoolThatKeepsNothing();
        // Stop part way through a run once the profile is not wanted. One run can be a whole layer of chunks.
        options.control.cancellation_requested = [&]() { return still_wanted && !still_wanted(); };
        const auto deadline = std::chrono::steady_clock::now() + _region_spectral_time;
        bool paused(false);
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
                // Partial sums. The same channels come again, complete.
                return true;
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                const auto z = first + static_cast<std::size_t>(block.first_channel + c);
                const auto counted = block.Totals(0, c);
                const double n = counted.num_pixels;
                num_pixels[z] = n;
                nan_count[z] = counted.nan_count;
                if (n == 0.0) {
                    // For a channel with no valid pixel, the other statistics stay NaN.
                    continue;
                }
                sum[z] = counted.sum;
                sum_sq[z] = counted.sum_sq;
                min[z] = counted.min;
                max[z] = counted.max;
                mean[z] = counted.sum / n;
                rms[z] = std::sqrt(counted.sum_sq / n);
                // Sigma from the centered moment. The formula sum_sq - sum^2 / n loses a small spread about a large mean.
                sigma[z] = n > 1.0 ? std::sqrt(counted.sum_sq_dev / (n - 1.0)) : 0.0;
                extrema[z] = std::abs(counted.min) > std::abs(counted.max) ? counted.min : counted.max;
                if (flux_density) {
                    (*flux_density)[z] = counted.sum / beam_area;
                }
            }
            state.channels_done = first + static_cast<std::size_t>(block.first_channel + block.channel_count);
            if (state.channels_done < last && std::chrono::steady_clock::now() >= deadline) {
                // Hand back what is finished
                paused = true;
                return false;
            }
            return true;
        };
        const auto reduced = image->ZarrImage().ReduceSpectral(request, store, options);
        // carta-zarr reports both stops as cancelled. At the deadline the call returns true. When the profile is not
        // wanted, it returns false. Neither stop logs a warning, and the finished channels are kept.
        if (!reduced && !paused) {
            if (reduced.error().code != carta::zarr::ErrorCode::cancelled) {
                spdlog::warn("Could not reduce a region over the spectrum of a Zarr image: {}", reduced.error().message);
            }
            pace.channels_done = std::max(pace.channels_done, state.channels_done);
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

bool ZarrLoader::GetRegionStats(int z, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
    std::map<CARTA::StatsType, double>& results) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || z < 0 || z >= _image_shape(2) || stokes < 0 || stokes >= _image_shape(3)) {
        return false;
    }
    const auto& mask_array = mask.asArray();
    const auto mask_shape = mask_array.shape();
    if (mask_shape.size() != 2 || origin.size() < 2 || !mask_array.contiguousStorage()) {
        return false;
    }
    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to lend a mask");
    const carta::zarr::RegionMask region{static_cast<std::uint64_t>(origin(0)), static_cast<std::uint64_t>(origin(1)),
        static_cast<std::uint64_t>(mask_shape(0)), static_cast<std::uint64_t>(mask_shape(1)),
        {reinterpret_cast<const std::uint8_t*>(mask_array.data()), static_cast<std::size_t>(mask_array.nelements())}};

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {static_cast<std::uint64_t>(z), 1, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = {&region, 1};
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum | carta::zarr::Statistic::sum_sq |
                         carta::zarr::Statistic::min | carta::zarr::Statistic::max | carta::zarr::Statistic::sum_sq_dev;

    std::optional<carta::zarr::SpectralTotals> totals;
    const auto store = [&](const carta::zarr::SpectralBlock& block) {
        if (block.complete && block.channel_count == 1) {
            totals = block.Totals(0, 0);
        }
        return true;
    };
    // Read through the shared cache. The region is in the displayed plane, so its chunks are often there already.
    const auto reduced = image->ZarrImage().ReduceSpectral(request, store, carta::zarr::ReadOptions{});
    if (!reduced || !totals) {
        if (!reduced) {
            spdlog::warn("Could not reduce a region of a Zarr image: {}", reduced.error().message);
        }
        return false;
    }

    const double n = totals->num_pixels;
    results.clear();
    results[CARTA::StatsType::NumPixels] = n;
    for (const auto stat : {CARTA::StatsType::Sum, CARTA::StatsType::SumSq, CARTA::StatsType::Mean, CARTA::StatsType::RMS,
             CARTA::StatsType::Sigma, CARTA::StatsType::Min, CARTA::StatsType::Max, CARTA::StatsType::Extrema}) {
        results[stat] = DOUBLE_NAN;
    }
    if (n == 0.0) {
        // With no valid pixel, the other statistics stay NaN.
        return true;
    }
    results[CARTA::StatsType::Sum] = totals->sum;
    results[CARTA::StatsType::SumSq] = totals->sum_sq;
    results[CARTA::StatsType::Mean] = totals->sum / n;
    results[CARTA::StatsType::RMS] = std::sqrt(totals->sum_sq / n);
    // Sigma from the centered moment. The formula sum_sq - sum^2 / n loses a small spread about a large mean.
    results[CARTA::StatsType::Sigma] = n > 1.0 ? std::sqrt(totals->sum_sq_dev / (n - 1.0)) : 0.0;
    results[CARTA::StatsType::Min] = totals->min;
    results[CARTA::StatsType::Max] = totals->max;
    results[CARTA::StatsType::Extrema] = std::abs(totals->min) > std::abs(totals->max) ? totals->min : totals->max;
    return true;
}

// A PV image is the mean spectra of many small boxes along a line, and the boxes overlap. carta-zarr reduces all of them
// in one pass, so each chunk that they cover is decoded once instead of once for each box.
FileLoader::LoaderScan ZarrLoader::GetRegionMeanSpectra(const std::vector<LoaderRegion>& regions, const AxisRange& z_range, int stokes,
    const std::function<bool()>& still_wanted, const std::function<void(float)>& progress, const RegionMeansSink& store) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || regions.empty() || !store || stokes < 0 || stokes >= _image_shape(3)) {
        return LoaderScan::unsupported;
    }
    const int depth = _image_shape(2);
    if (z_range.from < 0 || z_range.to < z_range.from || z_range.to >= depth) {
        return LoaderScan::unsupported;
    }
    const auto channels = static_cast<std::size_t>(z_range.to - z_range.from + 1);

    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to lend a mask");
    std::vector<carta::zarr::RegionMask> zarr_regions;
    zarr_regions.reserve(regions.size());
    for (const auto& region : regions) {
        if (region.origin.size() < 2 || region.shape.size() < 2 || region.shape(0) < 1 || region.shape(1) < 1) {
            return LoaderScan::unsupported;
        }
        carta::zarr::RegionMask zarr_region{static_cast<std::uint64_t>(region.origin(0)), static_cast<std::uint64_t>(region.origin(1)),
            static_cast<std::uint64_t>(region.shape(0)), static_cast<std::uint64_t>(region.shape(1))};
        const auto& mask_array = region.mask.asArray();
        if (mask_array.shape().size() != 2 || mask_array.shape()(0) != region.shape(0) || mask_array.shape()(1) != region.shape(1) ||
            !mask_array.contiguousStorage()) {
            return LoaderScan::unsupported;
        }
        zarr_region.mask = {reinterpret_cast<const std::uint8_t*>(mask_array.data()), static_cast<std::size_t>(mask_array.nelements())};
        zarr_regions.push_back(zarr_region);
    }

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {static_cast<std::uint64_t>(z_range.from), static_cast<std::uint64_t>(channels), 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = {zarr_regions.data(), zarr_regions.size()};
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum;

    carta::zarr::ReadOptions options;
    // The pass decodes each chunk once, so keeping the chunks would only evict those of the displayed plane.
    options.control.cache_pool = PoolThatKeepsNothing();
    options.control.cancellation_requested = [&]() { return still_wanted && !still_wanted(); };

    std::vector<float> run;
    std::size_t channels_done(0);
    bool missing_statistic(false);
    const auto take = [&](const carta::zarr::SpectralBlock& block) {
        // carta-zarr does not check cancellation_requested while it accumulates the last read, so the sink checks it too
        if (still_wanted && !still_wanted()) {
            return false;
        }
        if (!block.complete) {
            // Partial sums. The same channels come again, complete.
            if (progress) {
                progress((static_cast<float>(channels_done) + static_cast<float>(block.channel_count * block.completeness)) /
                         static_cast<float>(channels));
            }
            return true;
        }
        for (std::size_t r = 0; r < zarr_regions.size(); ++r) {
            const double* num_pixels = block.Series(r, carta::zarr::Statistic::num_pixels);
            const double* sum = block.Series(r, carta::zarr::Statistic::sum);
            if (!num_pixels || !sum) {
                missing_statistic = true;
                return false;
            }
            run.assign(static_cast<std::size_t>(block.channel_count), FLOAT_NAN);
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                if (num_pixels[c] > 0.0) {
                    run[c] = static_cast<float>(sum[c] / num_pixels[c]);
                }
            }
            store(r, static_cast<std::size_t>(block.first_channel), run.size(), run.data());
        }
        channels_done += static_cast<std::size_t>(block.channel_count);
        if (progress) {
            progress(static_cast<float>(channels_done) / static_cast<float>(channels));
        }
        return true;
    };
    const auto reduced = image->ZarrImage().ReduceSpectral(request, take, options);
    if (still_wanted && !still_wanted()) {
        return LoaderScan::cancelled;
    }
    if (missing_statistic || (reduced && channels_done != channels)) {
        spdlog::warn("Could not reduce the regions of a line over the spectrum of a Zarr image: a result was missing");
        return LoaderScan::failed;
    }
    return ScanOutcome(reduced, "reduce the regions of a line over the spectrum");
}

std::optional<ZarrPlaneRead> ZarrLoader::Plane(int z, int stokes) const {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || z < 0 || z >= _image_shape(2) || stokes < 0 || stokes >= _image_shape(3)) {
        return std::nullopt;
    }
    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, z, stokes), casacore::IPosition(4, _image_shape(0), _image_shape(1), 1, 1));
    // A plane is read with the default options, so through the shared cache. Reading ahead decodes into that cache.
    return ZarrPlaneRead{image->ZarrImage(), carta::zarr::ReadOptions{}, image->Request(plane)};
}

FileLoader::LoaderScan ZarrLoader::GetPlaneStats(int stokes, const std::function<bool()>& cancelled, const PlaneStatsCallback& plane) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || !plane || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3)) {
        return LoaderScan::unsupported;
    }
    const auto width = static_cast<std::uint64_t>(_image_shape(0));
    const auto height = static_cast<std::uint64_t>(_image_shape(1));
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));

    // One region that covers the whole plane, with no mask
    carta::zarr::RegionMask region;
    region.width = width;
    region.height = height;
    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = {&region, 1};
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum | carta::zarr::Statistic::sum_sq |
                         carta::zarr::Statistic::min | carta::zarr::Statistic::max | carta::zarr::Statistic::sum_sq_dev;

    carta::zarr::ReadOptions options;
    // The chunks are not kept. Even when the whole cube fits in the cache, the histogram pass is no faster with them.
    options.control.cache_pool = PoolThatKeepsNothing();
    options.control.cancellation_requested = cancelled;

    const auto reduced = image->ZarrImage().ReduceSpectral(
        request,
        [&](const carta::zarr::SpectralBlock& block) {
            if (!block.complete) {
                return true;
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                if (!plane(static_cast<int>(block.first_channel + c), ToBasicStats(block.Totals(0, c)))) {
                    return false;
                }
            }
            return true;
        },
        options);
    return ScanOutcome(reduced, "compute the plane statistics");
}

FileLoader::LoaderScan ZarrLoader::GetPlaneHistograms(
    int stokes, int num_bins, const HistogramBounds& bounds, const std::function<bool()>& cancelled, const PlaneHistogramCallback& plane) {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    // An empty range gets one bin over [0, 0] from CalcHistogram. carta-zarr cannot make that, so the caller does.
    // carta-zarr also refuses more than kMaxHistogramBins bins and bounds that do not fit in a float.
    const auto fits_float = [](double value) {
        return std::isfinite(value) && std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
    };
    if (!image || !plane || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3) || num_bins <= 0 ||
        static_cast<std::uint64_t>(num_bins) > carta::zarr::kMaxHistogramBins || !fits_float(bounds.min) || !fits_float(bounds.max) ||
        !(bounds.min < bounds.max)) {
        return LoaderScan::unsupported;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));

    carta::zarr::HistogramRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bin_count = static_cast<std::uint32_t>(num_bins);
    request.lower = bounds.min;
    request.upper = bounds.max;

    carta::zarr::ReadOptions options;
    options.control.cache_pool = PoolThatKeepsNothing();
    options.control.cancellation_requested = cancelled;

    std::vector<int> bins(static_cast<std::size_t>(num_bins));
    bool capped(false);
    const auto computed = image->ZarrImage().ComputeHistogram(
        request,
        [&](const carta::zarr::HistogramBlock& block) {
            if (!block.complete) {
                return true;
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                // A Histogram counts in int. A larger count is held at the largest int.
                const auto* counts = block.Counts(c);
                for (std::size_t b = 0; b < bins.size(); ++b) {
                    capped = capped || counts[b] > static_cast<std::uint64_t>(std::numeric_limits<int>::max());
                    bins[b] = static_cast<int>(std::min<std::uint64_t>(counts[b], std::numeric_limits<int>::max()));
                }
                Histogram histogram(num_bins, bounds, nullptr, 0);
                histogram.SetHistogramBins(bins);
                if (!plane(static_cast<int>(block.first_channel + c), histogram)) {
                    return false;
                }
            }
            return true;
        },
        options);
    if (capped) {
        spdlog::warn(
            "A Zarr plane histogram has a bin with more than {} pixels. It is shown as that many.", std::numeric_limits<int>::max());
    }
    return ScanOutcome(computed, "compute the plane histograms");
}

} // namespace carta
