/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARR_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARR_ZARRLOADER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../FileLoader.h"
#include "Util/Image.h"
#include "Util/Key.h"

namespace carta {

// Returns true if a column of `chunks_x` by `chunks_y` by `chunks_z` chunks, each of `chunk_bytes`,
// fits in a cache of `cache_bytes`.
bool SpectrumKeepsItsChunks(
    std::uint64_t chunks_x, std::uint64_t chunks_y, std::uint64_t chunks_z, std::uint64_t chunk_bytes, std::size_t cache_bytes);

class ZarrLoader : public FileLoader {
public:
    explicit ZarrLoader(const std::string& filename);

    // Reads the spectrum of one pixel. Returns false for several pixels, for a pixel or stokes outside the image,
    // and for a failed or stopped read.
    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const SpectralReadProgress& progress) override;
    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress, const SpectralReadWanted& still_wanted) override;

protected:
    void AllocateImage(const std::string& hdu) override;

    // How long one GetRegionSpectralData call reads before it returns. It matches the region handler's interval.
    // A shorter time does not help. With calls of 50 ms, profiles took up to twice as long.
    // _region_spectral_channels_per_emit is how many channels carta-zarr finishes at a time. Zero lets carta-zarr choose.
    // Both are protected, so that a test can change them.
    std::chrono::milliseconds _region_spectral_time{TARGET_PARTIAL_REGION_TIME};
    std::uint32_t _region_spectral_channels_per_emit = 0;

    // The key of a profile is the region's bounding box and the channel range. A new key or a new mask starts the
    // profile again. The pace keeps the mask to compare, because an edited region can keep its bounding box.
    struct RegionSpectralKey : public Key<int, int, std::int64_t, std::int64_t, std::int64_t, std::int64_t> {
        RegionSpectralKey() {}
        RegionSpectralKey(const casacore::IPosition& origin, const casacore::IPosition& mask_shape, const AxisRange& range)
            : Key({range.from, range.to, mask_shape(0), mask_shape(1), origin(0), origin(1)}) {}
    };

private:
    // The progress and statistics of one stokes of a region's profile
    struct RegionSpectralState {
        std::size_t channels_done = 0;
        std::map<CARTA::StatsType, std::vector<double>> stats;
        // The last read of this stokes by each thread, for the progress
        struct Read {
            std::int64_t read_index = -1;
            std::size_t channels_done = 0;
        };
        std::map<std::thread::id, Read> reads;
    };

    // How far the profiles of one region have got, across all stokes.
    // A computed stokes, such as Plinear, reads several stokes in turn and reports one progress. So a stokes reads
    // no further than the furthest one, and every call stops after _region_spectral_time. A call's progress is the
    // smallest among this stokes and the stokes that this thread read since it last read this one.
    // The mutex is held for a whole call, so the profiles of one region take turns. Other regions do not wait.
    struct RegionSpectralPace {
        std::mutex mutex;
        RegionSpectralKey key;
        // The mask that the profile is of. It shares the caller's storage, so it costs no copy.
        casacore::Array<casacore::Bool> mask;
        std::size_t channels_done = 0;
        std::int64_t reads = 0;
        // The state of each stokes, by stokes index
        std::map<int, RegionSpectralState> stokes;

        // Whether the profile is of `other_key` and `other_mask`
        bool IsOf(const RegionSpectralKey& other_key, const casacore::Array<casacore::Bool>& other_mask) const;
        // Starts every stokes of the region again, for `other_key` and `other_mask`
        void Reset(const RegionSpectralKey& other_key, const casacore::Array<casacore::Bool>& other_mask);
    };

    // Held only while finding or adding a pace. A pace is never erased.
    std::mutex _region_spectral_paces_mutex;
    // The pace of each region, by region id
    std::map<int, RegionSpectralPace> _region_spectral_paces;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARR_ZARRLOADER_H_
