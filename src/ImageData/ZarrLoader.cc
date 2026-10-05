/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include "CartaZarrImage.h"
#include "ImageStats/DerivedStatistics.h"
#include "ZarrContext.h"
#include "Util/Nan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

namespace carta {

namespace {

// A pool of no bytes, for a scan that comes back to no chunk to read through rather than evict what the
// session is looking at; the session's own if one cannot be made, which costs the session its working
// set rather than the scan its answer.
std::optional<carta::zarr::CachePool> KeepingNothing() {
    auto pool = GetZarrContext().NewCachePool(0);
    if (!pool) {
        spdlog::warn("A cube scan reads through the session's cache: {}", pool.error().message);
        return std::nullopt;
    }
    return *pool;
}

// What a reduction counts, as the statistics the caller's own calculator would have produced from
// the same pixels. Every reduction that comes here asks for sum_sq_dev, which sigma is made from; a
// cube histogram always counts it. A statistic that was not accumulated reads as SpectralTotals
// says -- zero for the sums, NaN for the extrema -- which is the value that calculator would have
// left there.
//
// A plane's totals and a cube histogram's are the same type, so this is also what turns a one-pass
// cube histogram, and each snapshot it hands out on the way, into the statistics CARTA reports.
//
// An empty plane is the case worth naming, and it is what BasicStats() is: nothing derived, as the
// calculator says, and the extrema at the identities it started from rather than the NaN a reduction
// reports for an untouched extremum, since callers compare against those.
BasicStats<float> ToPlaneStats(const carta::zarr::SpectralTotals& counted) {
    const auto count = static_cast<std::size_t>(counted.num_pixels);
    if (count == 0) {
        return BasicStats<float>();
    }
    const auto derived = DeriveStatistics(
        {counted.num_pixels, counted.sum, counted.sum_sq, counted.min, counted.max, counted.sum_sq_dev}, LonePixelSigma::nan);
    return BasicStats<float>{count, counted.sum, derived.mean, derived.sigma, static_cast<float>(counted.min),
        static_cast<float>(counted.max), derived.rms, counted.sum_sq, counted.sum_sq_dev};
}

// What this loader makes of a library call, said once. The library already tells a caller's stop
// apart from a failure (ADR 0011 in carta-zarr) and reports every failure as a Result rather than
// an exception, including one thrown by the sink it was handed -- so there is nothing to catch here,
// and nothing to rebuild from a bool. A failure is logged with what was being done, which the
// library's message does not know.
template <typename T>
BatchOutcome Outcome(const carta::zarr::Result<T>& result, const char* what) {
    if (result) {
        return BatchOutcome::finished;
    }
    const auto& error = result.error();
    if (error.code == carta::zarr::ErrorCode::cancelled) {
        return BatchOutcome::cancelled;
    }
    if (error.node_path.empty()) {
        spdlog::warn("Could not {} of a Zarr dataset: {}", what, error.message);
    } else {
        spdlog::warn("Could not {} of a Zarr dataset: {} ({})", what, error.message, error.node_path);
    }
    return BatchOutcome::failed;
}

}  // namespace

bool AssignBinCounts(const std::uint64_t* counts, std::size_t size, std::vector<int>& bins) {
    constexpr auto kMost = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    bins.resize(size);
    bool held = false;
    for (std::size_t bin = 0; bin < size; ++bin) {
        held = held || counts[bin] > kMost;
        bins[bin] = static_cast<int>(std::min(counts[bin], kMost));
    }
    return held;
}

ZarrLoader::ZarrLoader(const std::string& filename) : FileLoader(filename) {}

// Six entry points asked this in six spellings, and two of them put the upper bound on `stokes` a
// dozen lines further down, after they had already begun working out shapes -- so whether a request
// was refused for being out of range or accepted and then refused for something else depended on
// which one you were reading.
std::shared_ptr<CartaZarrImage> ZarrLoader::ImageForStokes(int stokes) const {
    if (!_zarr_image || _num_dims != 4 || stokes < 0 || stokes >= _image_shape(3)) {
        return nullptr;
    }
    return _zarr_image;
}

void ZarrLoader::AllocateImage(const std::string& hdu) {
    if (_image && hdu == _hdu) {
        return;
    }

    auto image = std::make_shared<CartaZarrImage>(_filename, hdu);
    _image = image;
    _zarr_image = image;
    _hdu = hdu;
    _image_shape = image->shape();
    _num_dims = _image_shape.size();
    _has_pixel_mask = image->hasPixelMask();
    _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(
        static_cast<casacore::CoordinateSystem*>(image->coordinates().clone()));
    _data_type = image->InternalDataType();
}

BatchOutcome ZarrLoader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
    std::mutex& /*image_mutex*/, const std::function<bool()>& cancellation_requested,
    const std::function<bool(float progress)>& partial_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || count_x <= 0 || count_y <= 0 || cursor_x < 0 || cursor_y < 0) {
        return BatchOutcome::declined;
    }

    const auto width = _image_shape(0);
    const auto height = _image_shape(1);
    const auto depth = _image_shape(2);
    if (cursor_x > width || cursor_y > height || count_x > width - cursor_x || count_y > height - cursor_y) {
        return BatchOutcome::declined;
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

    // carta-zarr Image handles are immutable and safe for concurrent reads. The backend mutex is
    // intentionally not held across this I/O operation.
    const auto read = Outcome(image->Read(destination, section, options, progress), "read the cursor spectrum");
    if (read != BatchOutcome::finished) {
        // Stopped by the cursor moving on, or failed: either way what is in the buffer is a prefix
        // nobody wants now.
        data.clear();
    }
    return read;
}

// The batched path exists for the position-velocity generator, which asks for one small box per
// pixel along a line. carta-zarr walks the chunks once and accumulates whichever boxes land on
// each, so the cost follows the chunks the boxes cover rather than the number of boxes.
BatchOutcome ZarrLoader::RegionSpectra(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
    const std::function<bool(const RegionSpectralBlock&)>& sink) {
    auto image = ImageForStokes(stokes);
    if (!image || regions.empty() || !sink) {
        return BatchOutcome::declined;
    }

    const auto depth = _image_shape(2);
    if (z_range.from < 0 || z_range.to < z_range.from || z_range.to >= depth) {
        return BatchOutcome::declined;
    }

    // casacore stores a Bool as one byte and an LCRegionFixed's mask contiguously with x fastest,
    // which is what carta-zarr's RegionMask already asks for, so the masks are handed over as they
    // are rather than converted -- and the reduction turns them into the runs it walks, along
    // whichever axis the store varies fastest, which is nothing this loader has to know.
    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to borrow a mask");
    std::vector<carta::zarr::RegionMask> zarr_regions;
    zarr_regions.reserve(regions.size());
    for (const auto& region : regions) {
        // A RegionMaskSpec's raster describes exactly its bounding box -- RegionHandler refuses one
        // that does not -- so its length is the box's, and carta-zarr checks it against that.
        const auto* raster = reinterpret_cast<const std::uint8_t*>(region.mask);
        zarr_regions.push_back(carta::zarr::RegionMask{region.x_start, region.y_start, region.width, region.height,
            {raster, raster != nullptr ? static_cast<std::size_t>(region.width * region.height) : 0}});
    }

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {static_cast<std::uint64_t>(z_range.from), static_cast<std::uint64_t>(z_range.to - z_range.from + 1), 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = {zarr_regions.data(), zarr_regions.size()};
    // The generator wants a mean, and a mean is a sum over a count. Nothing else is read, so
    // nothing else is accumulated.
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum;

    carta::zarr::ReadOptions options;
    options.read_budget_bytes = _read_budget_bytes;

    // Made once and refilled for every block, so that handing a block on costs two pointers per
    // region rather than an allocation.
    RegionSpectralBlock forwarded;
    forwarded.region_count = zarr_regions.size();
    forwarded.num_pixels.resize(zarr_regions.size());
    forwarded.sums.resize(zarr_regions.size());

    const auto forward = [&](const carta::zarr::SpectralBlock& block) {
        forwarded.first_channel = static_cast<std::size_t>(block.first_channel);
        forwarded.channel_count = static_cast<std::size_t>(block.channel_count);
        // A block that takes more than one read arrives several times, filling in. The caller
        // needs to know which arrival is the answer, so this is passed on rather than dropped:
        // the generator wants the partials to show progress with.
        forwarded.complete = block.complete;
        forwarded.completeness = block.completeness;
        // Both were asked for above, so the block carries both.
        for (std::size_t r = 0; r < forwarded.region_count; ++r) {
            forwarded.num_pixels[r] = block.Series(r, carta::zarr::Statistic::num_pixels);
            forwarded.sums[r] = block.Series(r, carta::zarr::Statistic::sum);
        }
        return sink(forwarded);
    };
    return Outcome(image->Library().ReduceSpectral(request, forward, options), "reduce regions over the spectrum");
}

std::optional<ZarrPlaneRead> ZarrLoader::Plane(int z, int stokes) const {
    auto image = ImageForStokes(stokes);
    if (!image || z < 0 || z >= _image_shape(2)) {
        return std::nullopt;
    }
    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, z, stokes), casacore::IPosition(4, _image_shape(0), _image_shape(1), 1, 1));
    auto [request, options] = image->LibraryRead(plane);
    return ZarrPlaneRead{image->Library(), std::move(options), std::move(request)};
}

BatchOutcome ZarrLoader::PlaneStats(int stokes, const std::function<bool()>& cancellation_requested,
    const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || !plane_callback) {
        return BatchOutcome::declined;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0) {
        return BatchOutcome::declined;
    }

    // One region covering the plane, with no mask: the reduction's own six accumulators over the
    // whole image are exactly a plane's basic statistics, so this needs no reduction of its own.
    const carta::zarr::RegionMask region{0, 0, static_cast<std::uint64_t>(_image_shape(0)), static_cast<std::uint64_t>(_image_shape(1))};

    carta::zarr::SpectralReduceRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.regions = {&region, 1};
    request.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::sum |
                         carta::zarr::Statistic::sum_sq | carta::zarr::Statistic::min |
                         carta::zarr::Statistic::max | carta::zarr::Statistic::sum_sq_dev;

    // A scan over the cube should not evict the session's working set; see PlaneHistograms. The
    // callback below skips every plane that is not finished yet, so it cannot be where a stop is
    // noticed: that is asked of the walk itself, at every read.
    carta::zarr::ReadOptions options;
    options.control.cache_pool = KeepingNothing();
    options.control.cancellation_requested = cancellation_requested;
    options.read_budget_bytes = _read_budget_bytes;

    const auto report_plane = [&](const carta::zarr::SpectralBlock& block) {
        if (!block.complete) {
            return true;  // a plane is reported when it is final, not while it fills
        }
        for (std::uint64_t c = 0; c < block.channel_count; ++c) {
            if (!plane_callback(static_cast<int>(block.first_channel + c),
                    ToPlaneStats(block.Totals(0, c)))) {
                return false;
            }
        }
        return true;
    };
    return Outcome(image->Library().ReduceSpectral(request, report_plane, options), "reduce the planes");
}

BatchOutcome ZarrLoader::PlaneHistograms(int stokes, int num_bins, const HistogramBounds& bounds,
    const std::function<bool()>& cancellation_requested, const std::function<bool(int z, const Histogram& histogram)>& plane_callback) {
    auto image = ImageForStokes(stokes);
    if (!image || !plane_callback) {
        return BatchOutcome::declined;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0 || num_bins <= 0 || !(bounds.min < bounds.max)) {
        // An empty or inverted range is the caller's degenerate case, which it answers with a single
        // bin over [0, 0]; that is not a shape this walk can produce, so it declines instead.
        return BatchOutcome::declined;
    }

    carta::zarr::HistogramRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bins = static_cast<std::uint32_t>(num_bins);
    request.lower = bounds.min;
    request.upper = bounds.max;

    // A cube scan touches every chunk once and reuses none of them, so it runs against a pool of
    // its own that holds nothing rather than evicting whatever the session is looking at. A stop is
    // asked of the walk at every read, as in PlaneStats.
    carta::zarr::ReadOptions options;
    options.control.cache_pool = KeepingNothing();
    options.control.cancellation_requested = cancellation_requested;
    options.read_budget_bytes = _read_budget_bytes;

    std::vector<int> plane_bins(static_cast<std::size_t>(num_bins));
    bool held = false;
    const auto report_plane = [&](const carta::zarr::HistogramBlock& block) {
        if (!block.complete) {
            return true;  // a plane is reported when it is final, not while it fills
        }
        for (std::uint64_t c = 0; c < block.channel_count; ++c) {
            held = AssignBinCounts(block.Counts(c), plane_bins.size(), plane_bins) || held;
            // Over the bounds it was asked to bin over, which is the only shape this walk produces.
            Histogram plane(num_bins, bounds, nullptr, 0);
            plane.SetHistogramBins(plane_bins);
            if (!plane_callback(static_cast<int>(block.first_channel + c), plane)) {
                return false;
            }
        }
        return true;
    };
    const auto outcome = Outcome(image->Library().ComputeHistogram(request, report_plane, options), "bin the planes");
    if (held) {
        spdlog::warn("A Zarr plane histogram has a bin past {} pixels; it is reported as that many",
            std::numeric_limits<int>::max());
    }
    return outcome;
}

BatchOutcome ZarrLoader::OnePassCubeHistogram(int stokes, int num_bins, std::uint64_t spatial_sample,
    const std::function<bool()>& cancellation_requested, BasicStats<float>& stats,
    std::vector<int>& bins, const std::function<bool(const CubeHistogramUpdate&)>& progress) {
    auto image = ImageForStokes(stokes);
    if (!image || num_bins <= 0 || spatial_sample == 0) {
        return BatchOutcome::declined;
    }
    const auto depth = static_cast<std::uint64_t>(_image_shape(2));
    if (depth == 0) {
        return BatchOutcome::declined;
    }

    carta::zarr::CubeHistogramRequest request;
    request.planes.spectral = {0, depth, 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bins = static_cast<std::uint32_t>(num_bins);
    request.spatial_sample = spatial_sample;
    carta::zarr::CubeHistogramProgressCallback zarr_progress;
    if (progress) {
        zarr_progress = [&progress](const carta::zarr::CubeHistogramProgress& update) {
            CubeHistogramUpdate reported;
            reported.progress = update.progress;
            // Kept lazy the whole way down: the library only re-aggregates if this is called, and
            // this is only called if the caller asks.
            reported.snapshot = [&update](BasicStats<float>& partial_stats, std::vector<int>& partial_bins) {
                const auto so_far = update.snapshot();
                partial_stats = ToPlaneStats(so_far.totals);
                // Not warned about here: a partial count only grows into the final one, which is.
                AssignBinCounts(so_far.counts.data(), so_far.counts.size(), partial_bins);
            };
            return progress(reported);
        };
    }

    // As with the other cube walks: read every chunk once, keep none of them, and ask for a stop at
    // every read -- not only through `progress`, which a walk done in one read never calls.
    carta::zarr::ReadOptions options;
    options.control.cache_pool = KeepingNothing();
    options.control.cancellation_requested = cancellation_requested;
    options.read_budget_bytes = _read_budget_bytes;

    auto result = image->Library().ComputeCubeHistogram(request, options, zarr_progress);
    const auto outcome = Outcome(result, "bin the cube in one pass");
    if (outcome != BatchOutcome::finished) {
        return outcome;
    }

    const auto& computed = result.value();
    stats = ToPlaneStats(computed.totals);
    if (AssignBinCounts(computed.counts.data(), computed.counts.size(), bins)) {
        spdlog::warn("A Zarr cube histogram has a bin past {} pixels; it is reported as that many",
            std::numeric_limits<int>::max());
    }
    return BatchOutcome::finished;
}

// Zarr has one copy of the pixels, so there is no second layout to choose between and no
// crossover to find: whatever the region's shape, this loader reads exactly the chunks it covers
// and accumulates in one pass, where casacore's route iterates cursors and asks for the mask
// separately. The HDF5 heuristic this replaces (height * depth < width) exists because HDF5 really
// does have two datasets on disk and picking the wrong one is expensive.
bool ZarrLoader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& /*image_mutex*/) {
    return _zarr_image != nullptr && _num_dims == 4 && region_shape.size() >= 2;
}

// A region's profile a run of whole channels at a time, finished in order: a channel reported
// complete is final, and those after it are unread until their turn. A run spanning more than one read
// is handed over as it fills, so the profile has something to show within one chunk layer of a large
// image, which is tens of seconds.
//
// The hint for how often the library hands over a run is left at zero on purpose: it then hands one
// over once per read budget, which is as often as it can without making the reads smaller, and that is
// what gives the deadline somewhere to fall.
BatchOutcome ZarrLoader::ReadOn(const RegionProfileRequest& request, std::mutex& /*image_mutex*/, RegionProfileProgress& progress,
    std::chrono::steady_clock::time_point deadline, const std::function<bool(double fraction)>& report) {
    auto image = ImageForStokes(request.stokes);
    if (!image || !request.mask || request.origin.size() < 2) {
        return BatchOutcome::declined;
    }
    const auto& mask = *request.mask;
    const auto mask_shape = mask.shape();
    // Any region of a four-axis image, whatever its shape: see UseRegionSpectralData.
    if (_num_dims != 4 || mask_shape.size() != 2 || !mask.asArray().contiguousStorage()) {
        return BatchOutcome::declined;
    }
    const int depth = _image_shape(2);
    const auto& range = request.channels;
    const auto channels = static_cast<std::size_t>(range.to - range.from + 1);
    if (range.from < 0 || range.to < range.from || range.to >= depth || progress.channels.size() != channels) {
        return BatchOutcome::declined;
    }
    if (progress.total == 0) {
        progress.total = channels;
    }
    if (progress.Complete()) {
        return BatchOutcome::finished;
    }

    static_assert(sizeof(casacore::Bool) == sizeof(std::uint8_t), "a casacore Bool must be one byte to borrow a mask");
    carta::zarr::RegionMask region{static_cast<std::uint64_t>(request.origin(0)), static_cast<std::uint64_t>(request.origin(1)),
        static_cast<std::uint64_t>(mask_shape(0)), static_cast<std::uint64_t>(mask_shape(1)),
        {reinterpret_cast<const std::uint8_t*>(mask.asArray().data()), static_cast<std::size_t>(mask.asArray().nelements())}};

    const auto first = static_cast<std::size_t>(progress.done);
    carta::zarr::SpectralReduceRequest reduce;
    reduce.planes.spectral = {static_cast<std::uint64_t>(range.from) + first, static_cast<std::uint64_t>(channels - first), 1};
    reduce.planes.polarization = static_cast<std::uint64_t>(request.stokes);
    reduce.regions = {&region, 1};
    reduce.statistics = carta::zarr::Statistic::num_pixels | carta::zarr::Statistic::nan_count | carta::zarr::Statistic::sum |
                        carta::zarr::Statistic::sum_sq | carta::zarr::Statistic::min | carta::zarr::Statistic::max |
                        carta::zarr::Statistic::sum_sq_dev;

    bool paused = false;
    carta::zarr::ReadOptions options;
    options.read_budget_bytes = _read_budget_bytes;
    const auto store = [&](const carta::zarr::SpectralBlock& block) {
        for (std::size_t c = 0; c < static_cast<std::size_t>(block.channel_count); ++c) {
            const auto counted = block.Totals(0, c);
            auto& channel = progress.channels[first + static_cast<std::size_t>(block.first_channel) + c];
            channel.read = true;
            channel.num_pixels = counted.num_pixels;
            channel.nan_count = counted.nan_count;
            channel.sum = counted.sum;
            channel.sum_sq = counted.sum_sq;
            channel.min = counted.min;
            channel.max = counted.max;
            channel.sum_sq_dev = counted.sum_sq_dev;
        }
        const auto run_start = static_cast<double>(first + static_cast<std::size_t>(block.first_channel));
        if (!block.complete) {
            // Partial sums over the chunks read so far: nothing is finished, so how far the profile has
            // got does not move, and the same channels arrive again.
            return report((run_start + (block.completeness * static_cast<double>(block.channel_count))) / static_cast<double>(channels));
        }
        progress.done = first + static_cast<std::size_t>(block.first_channel + block.channel_count);
        if (progress.Complete()) {
            return true;
        }
        if (!report(static_cast<double>(progress.done) / static_cast<double>(channels))) {
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            // Only ever at the end of a run: a pause inside one would throw away its partial sums, and
            // the next step would read them again.
            paused = true;
            return false;
        }
        return true;
    };
    const auto outcome = Outcome(image->Library().ReduceSpectral(reduce, store, options), "reduce a region over the spectrum");
    // A pause is this reader's own stop, and the library reports it as any other; the flag is what
    // tells the two apart (ADR 0011 in carta-zarr).
    if (outcome == BatchOutcome::cancelled && paused) {
        return BatchOutcome::finished;
    }
    return outcome;
}

double ZarrLoader::BeamArea() {
    return CalculateBeamArea();
}

}  // namespace carta
