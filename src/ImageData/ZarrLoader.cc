/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"
#include "ZarrContext.h"
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

// The accumulators of one block, by name.
//
// A block carries whichever statistics were asked for, one after another, and says in `statistics`
// which slot is which -- so everything in this file has to look them up the same way, and three
// places did, in three spellings. A null pointer is a statistic this block does not carry; what
// that means is left to the caller, because the three do not agree about it.
struct BlockStatistics {
    const double* num_pixels = nullptr;
    const double* nan_count = nullptr;
    const double* sum = nullptr;
    const double* sum_sq = nullptr;
    const double* min = nullptr;
    const double* max = nullptr;
};

BlockStatistics UnpackStatistics(const carta::zarr::SpectralBlock& block) {
    BlockStatistics found;
    for (std::size_t slot = 0; slot < block.statistic_count; ++slot) {
        const double* values = block.values + (slot * block.statistic_stride);
        switch (block.statistics[slot]) {
            case carta::zarr::Statistic::num_pixels: found.num_pixels = values; break;
            case carta::zarr::Statistic::nan_count: found.nan_count = values; break;
            case carta::zarr::Statistic::sum: found.sum = values; break;
            case carta::zarr::Statistic::sum_sq: found.sum_sq = values; break;
            case carta::zarr::Statistic::min: found.min = values; break;
            case carta::zarr::Statistic::max: found.max = values; break;
        }
    }
    return found;
}

// The statistics that are divisions of the counted ones, derived in one place so that a profile
// and a BasicStats over the same pixels cannot drift apart. Only called with num_pixels above zero.
//
// `lone_pixel_sigma` is what a single valid pixel reports, where the sample standard deviation is
// undefined, and it is a parameter because the callers genuinely differ rather than by oversight:
// a profile answers zero, because Hdf5Loader does (Hdf5Loader.cc:365) and StoreSpectralBlock's
// promise below is about exactly that; a BasicStats answers NaN, because BasicStatsCalculator does.
struct DerivedStats {
    double mean = 0.0;
    double rms = 0.0;
    double sigma = 0.0;
};

DerivedStats Derive(double num_pixels, double sum, double sum_sq, double lone_pixel_sigma) {
    DerivedStats derived;
    derived.mean = sum / num_pixels;
    derived.rms = std::sqrt(sum_sq / num_pixels);
    derived.sigma = num_pixels > 1.0 ? std::sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1.0))
                                     : lone_pixel_sigma;
    return derived;
}

// CARTA asks for eleven statistics; carta-zarr accumulates the six they are all made of. The
// derivations here are the same ones Hdf5Loader performs on its own accumulators, deliberately so:
// a Zarr profile and an HDF5 profile of the same numbers have to agree to the last bit.
void StoreSpectralBlock(std::map<CARTA::StatsType, std::vector<double>>& stats, const carta::zarr::SpectralBlock& block,
    std::size_t channel_offset, double beam_area, bool has_flux) {
    const auto counted = UnpackStatistics(block);
    if (!counted.num_pixels || !counted.nan_count || !counted.sum || !counted.sum_sq || !counted.min || !counted.max) {
        return;
    }

    for (std::size_t c = 0; c < static_cast<std::size_t>(block.channel_count); ++c) {
        const auto z = channel_offset + static_cast<std::size_t>(block.first_channel) + c;
        const double n = counted.num_pixels[c];
        stats[CARTA::StatsType::NumPixels][z] = n;
        stats[CARTA::StatsType::NanCount][z] = counted.nan_count[c];
        if (n == 0.0) {
            // Everything but the two counts is undefined for a channel with no valid pixel, and the
            // profile is already NaN there.
            continue;
        }
        const double sum = counted.sum[c];
        const double sum_sq = counted.sum_sq[c];
        const double smallest = counted.min[c];
        const double largest = counted.max[c];
        const auto derived = Derive(n, sum, sum_sq, 0.0);
        stats[CARTA::StatsType::Sum][z] = sum;
        stats[CARTA::StatsType::SumSq][z] = sum_sq;
        stats[CARTA::StatsType::Min][z] = smallest;
        stats[CARTA::StatsType::Max][z] = largest;
        stats[CARTA::StatsType::Mean][z] = derived.mean;
        stats[CARTA::StatsType::RMS][z] = derived.rms;
        stats[CARTA::StatsType::Sigma][z] = derived.sigma;
        stats[CARTA::StatsType::Extrema][z] = std::abs(smallest) > std::abs(largest) ? smallest : largest;
        if (has_flux) {
            stats[CARTA::StatsType::FluxDensity][z] = sum / beam_area;
        }
    }
}

// The six accumulators a reduction produces, as the statistics the caller's own calculator would
// have produced from the same pixels.
//
// An empty plane is the case worth naming: BasicStatsCalculator leaves its extrema at the
// identities it started from rather than reporting NaN, and callers compare against those, so the
// NaN a reduction reports for an untouched extremum is turned back into them here.
BasicStats<float> PlaneStats(const carta::zarr::SpectralBlock& block, std::size_t channel) {
    const auto counted = UnpackStatistics(block);
    // A statistic this block does not carry reads as the value the caller's own calculator would
    // have left there, which is zero for the sums and NaN for the extrema.
    const auto at = [&](const double* values, double absent) { return values == nullptr ? absent : values[channel]; };

    const double num_pixels = at(counted.num_pixels, 0.0);
    const auto count = static_cast<std::size_t>(num_pixels);
    if (count == 0) {
        return BasicStats<float>();
    }
    const double sum = at(counted.sum, 0.0);
    const double sum_sq = at(counted.sum_sq, 0.0);
    const auto derived = Derive(num_pixels, sum, sum_sq, DOUBLE_NAN);
    return BasicStats<float>{count, sum, derived.mean, derived.sigma, static_cast<float>(at(counted.min, DOUBLE_NAN)),
        static_cast<float>(at(counted.max, DOUBLE_NAN)), derived.rms, sum_sq};
}

// The statistics CARTA reports, from what one pass counted. Shared by the answer and by the
// snapshots handed out on the way, which are the same shape over fewer pixels.
BasicStats<float> ToBasicStats(const carta::zarr::CubeHistogramResult& computed) {
    const auto count = static_cast<std::size_t>(computed.num_pixels);
    if (count == 0) {
        return BasicStats<float>();
    }
    const auto derived = Derive(computed.num_pixels, computed.sum, computed.sum_sq, DOUBLE_NAN);
    return BasicStats<float>{count, computed.sum, derived.mean, derived.sigma, static_cast<float>(computed.minimum),
        static_cast<float>(computed.maximum), derived.rms, computed.sum_sq};
}

}  // namespace

ZarrLoader::ZarrLoader(const std::string& filename) : FileLoader(filename) {}

// Six entry points asked this in six spellings, and two of them put the upper bound on `stokes` a
// dozen lines further down, after they had already begun working out shapes -- so whether a request
// was refused for being out of range or accepted and then refused for something else depended on
// which one you were reading.
std::shared_ptr<CartaZarrImage> ZarrLoader::ImageForStokes(int stokes) const {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    if (!image || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3)) {
        return nullptr;
    }
    return image;
}

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
    const std::function<bool()>& cancellation_requested, const std::function<bool(float progress)>& partial_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || count_x <= 0 || count_y <= 0 || cursor_x < 0 || cursor_y < 0) {
        return false;
    }

    const auto width = _image_shape(0);
    const auto height = _image_shape(1);
    const auto depth = _image_shape(2);
    if (cursor_x > width || cursor_y > height || count_x > width - cursor_x || count_y > height - cursor_y) {
        return false;
    }

    const casacore::IPosition start(4, cursor_x, cursor_y, 0, stokes);
    const casacore::IPosition length(4, count_x, count_y, depth, 1);
    const casacore::Slicer section(start, length);
    // NaN rather than zero for the part not read yet: with a partial callback the caller publishes
    // this buffer before it is full, and an unread channel must look unread rather than empty.
    data.assign(static_cast<std::size_t>(count_x) * static_cast<std::size_t>(count_y) * static_cast<std::size_t>(depth),
        FLOAT_NAN);
    casacore::Array<float> destination(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);
    carta::zarr::ReadOptions options;
    options.control.cancellation_requested = cancellation_requested;
    // Supplying a progress callback is what makes carta-zarr issue the read in chunk-aligned pieces,
    // so the profile arrives in a prefix that grows rather than all at the end. Where those pieces
    // fall is the library's decision: it is the one that knows how many chunks a request has to hold
    // before they can be decoded in parallel.
    carta::zarr::ProgressCallback progress;
    if (partial_callback) {
        progress = [&](std::size_t written, std::size_t total) {
            return partial_callback(total == 0 ? 1.0f : static_cast<float>(written) / static_cast<float>(total));
        };
    }

    try {
        // carta-zarr Image handles are immutable and safe for concurrent reads. The backend
        // mutex is intentionally not held across this I/O operation.
        image->Read(destination, section, options, progress);
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
void ZarrLoader::ReleaseRegion(int region_id) {
    std::scoped_lock lock(_region_spectral_mutex);
    if (region_id == ALL_REGIONS) {
        _region_spectral.clear();
        return;
    }
    // Keyed by region and stokes both, so one region is several entries.
    for (auto it = _region_spectral.begin(); it != _region_spectral.end();) {
        it = it->first.first == region_id ? _region_spectral.erase(it) : std::next(it);
    }
}

bool ZarrLoader::GetMultiRegionSpectralData(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
    const std::function<bool(const RegionSpectralBlock&)>& sink) {
    auto image = ImageForStokes(stokes);
    if (!image || regions.empty() || !sink) {
        return false;
    }

    const auto depth = _image_shape(2);
    if (z_range.from < 0 || z_range.to < z_range.from || z_range.to >= depth) {
        return false;
    }

    // casacore stores a Bool as one byte and an LCRegionFixed's mask contiguously with x fastest,
    // which is what carta-zarr's RegionMask already asks for, so the masks are handed over as they
    // are rather than converted.
    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to borrow a mask");
    std::vector<carta::zarr::RegionMask> zarr_regions;
    zarr_regions.reserve(regions.size());
    for (const auto& region : regions) {
        auto& spec = zarr_regions.emplace_back(carta::zarr::RegionMask{region.x_start, region.y_start, region.width,
            region.height, reinterpret_cast<const std::uint8_t*>(region.mask)});
        spec.row_runs = region.runs;
        spec.row_run_offsets = region.run_offsets;
        spec.run_axis =
            region.runs_along_y ? carta::zarr::AxisRole::spatial_y : carta::zarr::AxisRole::spatial_x;
    }

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {static_cast<std::uint64_t>(z_range.from), static_cast<std::uint64_t>(z_range.to - z_range.from + 1), 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = zarr_regions.data();
    request.region_count = zarr_regions.size();
    // The generator wants a mean, and a mean is a sum over a count. Nothing else is read, so
    // nothing else is accumulated.
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum;

    carta::zarr::ReadOptions options;
    options.temporary_memory_limit_bytes = _read_budget_bytes;

    try {
        return image->ReduceSpectral(request, [&](const carta::zarr::SpectralBlock& block) {
            RegionSpectralBlock forwarded;
            forwarded.first_channel = static_cast<std::size_t>(block.first_channel);
            forwarded.channel_count = static_cast<std::size_t>(block.channel_count);
            forwarded.region_count = zarr_regions.size();
            forwarded.region_stride = block.region_stride;
            // A block that takes more than one read arrives several times, filling in. The caller
            // needs to know which arrival is the answer, so this is passed on rather than dropped:
            // the generator wants the partials to show progress with.
            forwarded.complete = block.complete;
            forwarded.completeness = block.completeness;
            // Both statistics live in the one buffer; their slots are what the block declares.
            const auto counted = UnpackStatistics(block);
            forwarded.num_pixels = counted.num_pixels;
            forwarded.sum = counted.sum;
            if (forwarded.num_pixels == nullptr || forwarded.sum == nullptr) {
                return false;
            }
            return sink(forwarded);
        }, options);
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not reduce regions over the spectrum of a Zarr dataset: {}", error.getMesg());
        return false;
    }
}

bool ZarrLoader::GetCubeBasicStats(
    int stokes, const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || !plane_callback) {
        return false;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0) {
        return false;
    }

    // One region covering the plane, with no mask: the reduction's own six accumulators over the
    // whole image are exactly a plane's basic statistics, so this needs no reduction of its own.
    const carta::zarr::RegionMask region{0, 0, static_cast<std::uint64_t>(_image_shape(0)),
        static_cast<std::uint64_t>(_image_shape(1)), nullptr};

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = &region;
    request.region_count = 1;
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum |
                         carta::zarr::Statistic::sum_sq | carta::zarr::Statistic::min |
                         carta::zarr::Statistic::max;

    // As above: a scan over the cube should not evict the session's working set.
    carta::zarr::ReadOptions options;
    options.control.cache_policy = carta::zarr::CachePolicy::bypass;

    try {
        const bool finished = image->ReduceSpectral(request, [&](const carta::zarr::SpectralBlock& block) {
            if (!block.complete) {
                return true;  // a plane is reported when it is final, not while it fills
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                if (!plane_callback(static_cast<int>(block.first_channel + c),
                        PlaneStats(block, static_cast<std::size_t>(c)))) {
                    return false;
                }
            }
            return true;
        }, options);
        return finished;
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not reduce the planes of a Zarr dataset: {}", error.getMesg());
        return false;
    }
}

bool ZarrLoader::GetCubeHistogram(int stokes, int num_bins, const HistogramBounds& bounds,
    const std::function<bool(int z, const std::vector<int>& bins)>& plane_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || !plane_callback) {
        return false;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0 || num_bins <= 0 || !(bounds.min < bounds.max)) {
        // An empty or inverted range is the caller's degenerate case, which it answers with a single
        // bin over [0, 0]; that is not a shape this walk can produce, so it declines instead.
        return false;
    }

    carta::zarr::HistogramRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bins = static_cast<std::uint32_t>(num_bins);
    request.lower = bounds.min;
    request.upper = bounds.max;

    // A cube scan touches every chunk once and reuses none of them, so it runs against a pool of
    // its own that holds nothing rather than evicting whatever the session is looking at.
    carta::zarr::ReadOptions options;
    options.control.cache_policy = carta::zarr::CachePolicy::bypass;

    std::vector<int> plane_bins(static_cast<std::size_t>(num_bins));
    try {
        return image->ComputeHistogram(request, [&](const carta::zarr::HistogramBlock& block) {
            if (!block.complete) {
                return true;  // a plane is reported when it is final, not while it fills
            }
            for (std::uint64_t c = 0; c < block.channel_count; ++c) {
                const auto* row = block.counts + (static_cast<std::size_t>(c) * block.bin_count);
                for (std::size_t bin = 0; bin < plane_bins.size(); ++bin) {
                    plane_bins[bin] = static_cast<int>(row[bin]);
                }
                if (!plane_callback(static_cast<int>(block.first_channel + c), plane_bins)) {
                    return false;
                }
            }
            return true;
        }, options);
    } catch (const casacore::AipsError& error) {
        spdlog::warn("Could not bin the planes of a Zarr dataset: {}", error.getMesg());
        return false;
    }
}

bool ZarrLoader::GetCubeHistogramOnePass(int stokes, int num_bins, std::uint64_t spatial_sample,
    BasicStats<float>& stats, std::vector<int>& bins,
    const std::function<bool(const CubeHistogramUpdate&)>& progress) {
    const auto settings = GetZarrHistogramSettings();
    if (!settings.one_pass) {
        // Exact is the default, and the default is two passes. Declining here is what keeps the
        // caller's own loops in charge without it having to know this setting exists.
        return false;
    }
    auto image = ImageForStokes(stokes);
    if (!image || num_bins <= 0) {
        return false;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0) {
        return false;
    }

    carta::zarr::CubeHistogramRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bins = static_cast<std::uint32_t>(num_bins);
    request.spatial_sample = spatial_sample > 0 ? spatial_sample : settings.spatial_sample;
    if (progress) {
        request.progress = [&progress](const carta::zarr::CubeHistogramProgress& update) {
            CubeHistogramUpdate reported;
            reported.progress = update.progress;
            // Kept lazy the whole way down: the library only re-aggregates if this is called, and
            // this is only called if the caller asks.
            reported.snapshot = [&update](BasicStats<float>& partial_stats, std::vector<int>& partial_bins) {
                const auto so_far = update.snapshot();
                partial_stats = ToBasicStats(so_far);
                partial_bins.assign(so_far.counts.begin(), so_far.counts.end());
            };
            return progress(reported);
        };
    }

    // As with the other cube walks: read every chunk once, keep none of them.
    carta::zarr::ReadOptions options;
    options.control.cache_policy = carta::zarr::CachePolicy::bypass;
    options.temporary_memory_limit_bytes = _read_budget_bytes;

    auto result = image->ComputeCubeHistogram(request, options);
    if (!result) {
        if (result.error().code != carta::zarr::ErrorCode::cancelled) {
            spdlog::warn("Could not compute a Zarr cube histogram in one pass: {}", result.error().message);
        }
        return false;
    }

    const auto& computed = result.value();
    stats = ToBasicStats(computed);
    bins.assign(computed.counts.begin(), computed.counts.end());
    return true;
}

bool ZarrLoader::SpectralRunsAlongY() const {
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image);
    return image != nullptr && image->SpectralRunsAlongY();
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
// The hint is left at zero on purpose: the library then emits once per read budget, which is as
// often as it can without making the reads smaller. That is what gives the deadline below somewhere
// to fire -- asking for one block at the end would make this loop a single call however long it ran.
//
// Channels are finished in order rather than all of them being refined together: a channel this
// call reports is final, and the ones after it stay NaN until their turn.
bool ZarrLoader::GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& /*image_mutex*/,
    std::map<CARTA::StatsType, std::vector<double>>& results, float& progress,
    const std::function<bool(const std::map<CARTA::StatsType, std::vector<double>>&, float)>& partial_callback) {
    auto image = ImageForStokes(stokes);
    if (!image) {
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
        state.runs = RunsOfMask(mask, image->SpectralRunsAlongY());
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
        carta::zarr::RegionMask region{static_cast<std::uint64_t>(origin(0)), static_cast<std::uint64_t>(origin(1)),
            static_cast<std::uint64_t>(mask_shape(0)), static_cast<std::uint64_t>(mask_shape(1)),
            reinterpret_cast<const std::uint8_t*>(mask.asArray().data())};
        if (!state.runs.Empty()) {
            region.row_runs = state.runs.runs.data();
            region.row_run_offsets = state.runs.offsets.data();
            region.run_axis = image->SpectralRunsAlongY() ? carta::zarr::AxisRole::spatial_y
                                                          : carta::zarr::AxisRole::spatial_x;
        }

        carta::zarr::SpectralReduceRequest request;
        request.planes.spectral = {static_cast<std::uint64_t>(range.from + state.channels_done),
            static_cast<std::uint64_t>(channels - state.channels_done), 1};
        request.planes.polarization = static_cast<std::uint64_t>(stokes);
        request.regions = &region;
        request.region_count = 1;
        request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::nan_count |
                             carta::zarr::Statistic::sum | carta::zarr::Statistic::sum_sq |
                             carta::zarr::Statistic::min | carta::zarr::Statistic::max;

        const auto first_channel = state.channels_done;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TARGET_PARTIAL_REGION_TIME);
        bool paused = false;
        try {
            const auto report = [&](double done) {
                return !partial_callback ||
                       partial_callback(state.stats, static_cast<float>(done / static_cast<double>(channels)));
            };
            carta::zarr::ReadOptions options;
            options.temporary_memory_limit_bytes = _read_budget_bytes;
            const bool finished = image->ReduceSpectral(request, [&](const carta::zarr::SpectralBlock& block) {
                StoreSpectralBlock(state.stats, block, first_channel, beam_area, has_flux);
                if (!block.complete) {
                    // Partial sums over the chunks read so far. Forwarding them is the whole point
                    // of the callback: one chunk layer of a region covering a large image is tens of
                    // seconds, and this is the only thing the caller sees while it is being read.
                    // Nothing is finished, so channels_done does not move and the same channels
                    // arrive again.
                    return report(static_cast<double>(first_channel + block.first_channel) +
                                  (block.completeness * static_cast<double>(block.channel_count)));
                }
                state.channels_done = first_channel + static_cast<std::size_t>(block.first_channel + block.channel_count);
                if (state.channels_done >= channels) {
                    return true;
                }
                if (!report(static_cast<double>(state.channels_done))) {
                    return false;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    // Hand back what is finished so the caller can show it and decide whether this
                    // profile is still wanted. Only ever at a block boundary: a pause inside one
                    // would throw away the partial sums, and the next call would read them again.
                    paused = true;
                    return false;
                }
                return true;
            }, options);
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
