/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

#include "FileLoader.h"
#include "PlaneReadAhead.h"
#include "ImageStats/CubeReducer.h"
#include "ImageStats/RegionProfileReader.h"
#include "ImageStats/RegionReducer.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace carta {

class CartaZarrImage;

// Bin counts as the backend holds them: int, because CARTA's protocol carries a bin as sfixed32.
// The library counts in 64 bits, and a whole-cube histogram can pass INT_MAX in one bin; such a
// count is held at INT_MAX rather than wrapped to a negative one that every percentile read from it
// would inherit. Returns whether any count was held.
bool AssignBinCounts(const std::uint64_t* counts, std::size_t size, std::vector<int>& bins);

class ZarrLoader : public FileLoader,
                   public RegionReducer,
                   public CubeReducer,
                   public RegionProfileReader,
                   public PlaneReadAhead {
public:
    explicit ZarrLoader(const std::string& filename);

    BatchOutcome GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const std::function<bool()>& cancellation_requested = {},
        const std::function<bool(float progress)>& partial_callback = {}) override;

    CubeReducer* CubeWalk() override {
        return this;
    }
    RegionReducer* RegionWalk() override {
        return this;
    }
    BatchOutcome PlaneStats(int stokes, const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) override;
    BatchOutcome PlaneHistograms(int stokes, int num_bins, const HistogramBounds& bounds,
        const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const Histogram& histogram)>& plane_callback) override;
    BatchOutcome OnePassCubeHistogram(int stokes, int num_bins, std::uint64_t spatial_sample, BasicStats<float>& stats,
        std::vector<int>& bins, const std::function<bool(const CubeHistogramUpdate&)>& progress) override;
    BatchOutcome RegionSpectra(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink) override;

    RegionProfileReader* ProfileReader() override {
        return this;
    }
    PlaneReadAhead* ReadAhead() override {
        return this;
    }
    std::optional<ZarrPlaneRead> Plane(int z, int stokes) const override;
    PlaneRun RunOf(int z, int stokes) const override;
    std::uint64_t RunBytes() const override;
    std::uint64_t CacheBytes() const override;
    bool Prefetch(int z, int stokes, const std::function<bool()>& cancelled) override;
    BatchOutcome ReadOn(const RegionProfileRequest& request, std::mutex& image_mutex, RegionProfileProgress& progress,
        std::chrono::steady_clock::time_point deadline, const std::function<bool(double fraction)>& report) override;
    double BeamArea() override;

    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;

    // The read budget one walk may hold, in bytes; zero leaves the library its own.
    // Exists so that a test can make a walk take more than one read, which is what a region
    // covering a large image does and what no fixture small enough to keep in a repository can.
    // Every walk honours it. The one-pass cube histogram needs it for one more reason, since
    // it only reports its progress between reads, and the two per-plane walks for another: a
    // cancellation between reads can only be seen to arrive before a plane is finished if a plane
    // takes more than one read.
    void SetReadBudgetBytes(std::size_t bytes) {
        _read_budget_bytes = bytes;
    }

protected:
    void AllocateImage(const std::string& hdu) override;

    // The image this loader reads, when it can serve a request for `stokes` at all. Null means it
    // cannot, which every entry point here reports as false or as BatchOutcome::declined.
    std::shared_ptr<CartaZarrImage> ImageForStokes(int stokes) const;

    // _image, as the type it always is here. Set with it, so that asking for the library's walks
    // is not a cast on every call.
    std::shared_ptr<CartaZarrImage> _zarr_image;
    std::size_t _read_budget_bytes = 0;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
