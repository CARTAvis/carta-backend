/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

#include "BatchedReducer.h"
#include "FileLoader.h"
#include "ImageStats/CubeReducer.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace carta {

class CartaZarrImage;

// Bin counts as the backend holds them: int, because CARTA's protocol carries a bin as sfixed32.
// The library counts in 64 bits, and a whole-cube histogram can pass INT_MAX in one bin; such a
// count is held at INT_MAX rather than wrapped to a negative one that every percentile read from it
// would inherit. Returns whether any count was held.
bool AssignBinCounts(const std::uint64_t* counts, std::size_t size, std::vector<int>& bins);

class ZarrLoader : public FileLoader, public BatchedReducer, public CubeReducer {
public:
    explicit ZarrLoader(const std::string& filename);

    bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex,
        const std::function<bool()>& cancellation_requested = {},
        const std::function<bool(float progress)>& partial_callback = {}) override;

    CubeReducer* CubeWalk() override {
        return this;
    }
    BatchedReducer* Batched() override {
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

    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& image_mutex,
        std::map<CARTA::StatsType, std::vector<double>>& results, float& progress,
        const std::function<bool(const std::map<CARTA::StatsType, std::vector<double>>&, float)>& partial_callback = {}) override;

    // The read budget one batched walk may hold, in bytes; zero leaves the library its own.
    // Exists so that a test can make a walk take more than one read, which is what a region
    // covering a large image does and what no fixture small enough to keep in a repository can.
    // Every batched walk honours it. The one-pass cube histogram needs it for one more reason, since
    // it only reports its progress between reads, and the two per-plane walks for another: a
    // cancellation between reads can only be seen to arrive before a plane is finished if a plane
    // takes more than one read.
    void SetReadBudgetBytes(std::size_t bytes) {
        _read_budget_bytes = bytes;
    }

    void ReleaseRegion(int region_id) override;

protected:
    // What one region's profile has accumulated so far.
    //
    // GetRegionSpectralData is called in a loop until it reports completion, because that loop is
    // where the caller checks whether the region still exists and whether the user still wants the
    // answer. A whole-cube profile is seconds of work, so it has to be interruptible somewhere, and
    // the seam between calls is the only one the interface offers.
    //
    // It looks like a second way to stop beside partial_callback, and it is not one to remove. Two
    // callers depend on it that the callback cannot serve:
    //
    // - A computed stokes profile (Ptotal, Plinear, Pangle and the fractional ones) asks for Q, U
    //   and sometimes I and V one after another with no partial_callback, since no single stokes'
    //   partial makes a partial of theirs. RegionHandler::GetComputedStokesProfiles gets its partials
    //   and its between-call checks only because each call returns after one slice: the loop
    //   advances every stokes a slice at a time. Run to completion, it would read them whole in turn
    //   with nothing shown and no way to stop.
    // - A finished state is the loader path's only answer cache. RegionHandler fills its own
    //   spectral cache only on the route that reads through casacore, so a second request for the
    //   same region and stokes -- a statistic added, another profile of it -- is free only because
    //   channels_done already covers the range.
    //
    // Hdf5Loader keeps the same per-region, per-stokes state for the same two reasons: it is how
    // FileLoader::GetRegionSpectralData is meant to be implemented, not something this loader added.
    //
    // Each state has its own lock, held for the whole of one call's walk, so that two calls for the
    // same region and stokes take turns rather than both writing its stats.
    struct RegionSpectralState {
        std::mutex mutex;
        casacore::IPosition origin;
        casacore::IPosition shape;
        AxisRange z_range;
        std::size_t channels_done = 0;
        std::map<CARTA::StatsType, std::vector<double>> stats;
    };

    void AllocateImage(const std::string& hdu) override;

    // The image this loader reads, when it can serve a request for `stokes` at all. Null means it
    // cannot, which every entry point here reports as false or as BatchOutcome::declined.
    std::shared_ptr<CartaZarrImage> ImageForStokes(int stokes) const;

    // _image, as the type it always is here. Set with it, so that asking for the library's walks
    // is not a cast on every call.
    std::shared_ptr<CartaZarrImage> _zarr_image;
    std::size_t _read_budget_bytes = 0;
    // Guards the map and nothing in it: held only to find, add, or drop an entry, never across a
    // walk. A walk holds its state through the shared_ptr, so a release that drops the entry while
    // the walk is running leaves it to finish into a state nobody will ask for again.
    std::mutex _region_spectral_mutex;
    // Reachable by a subclass so a test can see that a removed region's state went with it: the
    // only other evidence is memory that is not freed, which nothing can assert on.
    std::map<std::pair<int, int>, std::shared_ptr<RegionSpectralState>> _region_spectral;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
