/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrLoader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <spdlog/spdlog.h>

#include "CartaZarrImage.h"
#include "ZarrContext.h"

namespace carta {

namespace {

// The same empty/constant-range convention as CalcHistogram, without a second read of the pixels.
void MakeCubeHistogram(const carta::zarr::CubeHistogramResult& result, int num_bins,
    BasicStats<float>& stats, Histogram& histogram) {
    const auto& totals = result.totals;
    stats = BasicStats<float>();
    if (totals.num_pixels > 0) {
        const double variance = totals.num_pixels > 1 ? std::max(0.0, totals.sum_sq_dev / (totals.num_pixels - 1))
                                                     : std::numeric_limits<double>::quiet_NaN();
        stats = BasicStats<float>(static_cast<std::size_t>(totals.num_pixels), totals.sum,
            totals.sum / totals.num_pixels, std::sqrt(variance), static_cast<float>(totals.min), static_cast<float>(totals.max),
            std::sqrt(totals.sum_sq / totals.num_pixels), totals.sum_sq);
    }
    const auto cap_count = [](std::uint64_t count) {
        return static_cast<int>(std::min(count, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    };
    const HistogramBounds bounds(stats.min_val, stats.max_val);
    if (stats.num_pixels == 0 || bounds.Invalid<float>()) {
        histogram = Histogram(1, HistogramBounds(0, 0), nullptr, 0);
        histogram.SetHistogramBins({stats.num_pixels > 0 && stats.min_val == 0 ? cap_count(stats.num_pixels) : 0});
        return;
    }
    std::vector<int> bins;
    bins.reserve(result.counts.size());
    for (auto count : result.counts) {
        bins.push_back(cap_count(count));
    }
    histogram = Histogram(num_bins, bounds, nullptr, 0);
    histogram.SetHistogramBins(bins);
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

std::optional<ZarrPlaneRead> ZarrLoader::Plane(int z, int stokes) const {
    if (!_image || _num_dims != 4 || z < 0 || z >= _image_shape(2) || stokes < 0 || stokes >= _image_shape(3)) {
        return std::nullopt;
    }
    return static_cast<const CartaZarrImage&>(*_image).PlaneRead(z, stokes);
}

CubeHistogramStatus ZarrLoader::OnePassCubeHistogram(int stokes, int num_bins, BasicStats<float>& stats, Histogram& histogram,
    const std::function<bool()>& cancelled, const CubeHistogramCallback& progress) {
    const auto plane = Plane(0, stokes);
    if (!plane || num_bins <= 0) {
        return CubeHistogramStatus::declined;
    }
    if (cancelled && cancelled()) {
        return CubeHistogramStatus::cancelled;
    }
    carta::zarr::CubeHistogramRequest request;
    request.planes.spectral = {0, static_cast<std::uint64_t>(_image_shape(2)), 1};
    request.planes.polarization = static_cast<std::uint64_t>(stokes);
    request.bin_count = static_cast<std::uint32_t>(num_bins);
    request.spatial_sample = 1;

    carta::zarr::ReadOptions options;
    options.control.cancellation_requested = cancelled;
    options.read_budget_bytes = _histogram_read_budget_bytes;
    // A cube scan has no reuse; avoid evicting the chunks currently used for rendering/animation.
    auto pool = GetZarrContext().CreateCachePool(0);
    if (!pool) {
        spdlog::error("Cannot create a cache for the Zarr cube histogram: {}", pool.error().message);
        return CubeHistogramStatus::failed;
    }
    options.control.cache_pool = *pool;
    carta::zarr::CubeHistogramProgressCallback report;
    if (progress) {
        report = [&](const carta::zarr::CubeHistogramProgress& update) {
            CubeHistogramProgress forwarded;
            forwarded.progress = update.completeness;
            forwarded.snapshot = [&](BasicStats<float>& partial_stats, Histogram& partial) {
                MakeCubeHistogram(update.snapshot(), num_bins, partial_stats, partial);
            };
            return progress(forwarded);
        };
    }
    auto result = plane->image.ComputeCubeHistogram(request, options, report);
    if (!result) {
        if (result.error().code == carta::zarr::ErrorCode::cancelled) {
            return CubeHistogramStatus::cancelled;
        }
        spdlog::error("Cannot compute Zarr cube histogram: {}", result.error().message);
        return CubeHistogramStatus::failed;
    }
    if (cancelled && cancelled()) {
        return CubeHistogramStatus::cancelled;
    }
    if (std::any_of(result->counts.begin(), result->counts.end(), [](auto count) {
            return count > static_cast<std::uint64_t>(std::numeric_limits<int>::max());
        })) {
        spdlog::warn("A Zarr cube histogram bin exceeds INT_MAX; reporting INT_MAX for that bin");
    }
    MakeCubeHistogram(*result, num_bins, stats, histogram);
    return CubeHistogramStatus::finished;
}

} // namespace carta
