/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

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

namespace carta {

// Whether a spectrum read keeps its chunks in the shared cache. The spectrum covers a column of
// `chunks_x` by `chunks_y` by `chunks_z` chunks, and each chunk decodes to `chunk_bytes`.
// It returns true when the whole column fits in a cache of `cache_bytes`. Then a cursor that moves
// within the same chunks reads them from the cache.
bool SpectrumKeepsItsChunks(
    std::uint64_t chunks_x, std::uint64_t chunks_y, std::uint64_t chunks_z, std::uint64_t chunk_bytes, std::size_t cache_bytes);

class ZarrLoader : public FileLoader {
public:
    explicit ZarrLoader(const std::string& filename);

    // Reads the spectrum of one pixel with carta-zarr. Returns false for an image that is not 4-D, for more than
    // one pixel, for a pixel or stokes outside the image, and when the read fails or is stopped.
    // It calls `progress` with the number of final channels as the read goes. The read stops if `progress` returns false.
    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const SpectralReadProgress& progress) override;
    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress, const SpectralReadWanted& still_wanted) override;

protected:
    void AllocateImage(const std::string& hdu) override;

    // How long one GetRegionSpectralData call reads before it hands back what is finished.
    // It is the same as the time the region handler waits between partial results. A shorter time does not help.
    // It would not stop a profile sooner, because the handler's checks reach a call while it reads.
    // It would not let other regions read sooner, because they do not wait for this one.
    // With calls of 50 ms, profiles took up to twice as long.
    //
    // _region_spectral_channels_per_emit is how many channels carta-zarr finishes at a time. Zero lets carta-zarr choose.
    //
    // Both are protected, so that a test can make a call pause after every run, or never.
    std::chrono::milliseconds _region_spectral_time{TARGET_PARTIAL_REGION_TIME};
    std::uint32_t _region_spectral_channels_per_emit = 0;

    // What a profile is of: the region and the channel range. Another region or range starts the profile again.
    // The key keeps a hash of the mask's pixels and their count, not a copy of the mask.
    // For a large region, a copy would be tens of MB, kept for as long as the image is open.
    // It is protected, so that a test can tell the keys of two masks apart.
    struct RegionSpectralKey {
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t width = 0;
        std::int64_t height = 0;
        std::uint64_t mask_hash = 0;
        std::size_t mask_pixels = 0;
        int z_from = 0;
        int z_to = -1;

        static RegionSpectralKey Of(const casacore::IPosition& origin, const casacore::Array<casacore::Bool>& mask, const AxisRange& range);
        bool operator==(const RegionSpectralKey& other) const;
    };

private:
    // What one region's profile of one stokes has so far. The caller calls GetRegionSpectralData until
    // progress is 1. Between calls, it checks that the region and the request have not changed.
    struct RegionSpectralState {
        std::size_t channels_done = 0;
        std::map<CARTA::StatsType, std::vector<double>> stats;
        // The last read of this stokes by each thread. It keeps the read's index among the region's reads,
        // and how many channels the thread was given then. One profile is read on one thread from start to end.
        struct Read {
            std::int64_t read_index = -1;
            std::size_t channels_done = 0;
        };
        std::map<std::thread::id, Read> reads;
    };

    // How far the profiles of one region have got, across all stokes.
    //
    // A computed stokes, such as Plinear, is made from several stokes. The caller reads them in turn, call
    // after call, and reports one progress for all of them. So that they advance together:
    // - A stokes behind the furthest one reads no further than it.
    // - Every call stops after _region_spectral_time.
    // - A call's progress is the smallest number of channels that this thread was given, for this stokes
    //   and for every stokes the thread read since it last read this one. Those are the stokes that this one
    //   is combined with. So a progress of 1 means that all of them are finished, even if another profile
    //   of the region read more in the meantime.
    //
    // The mutex is held for a whole call, so that the profiles of one region take turns. The profiles of
    // other regions do not wait for it. This matters because a region that covers a large image reads
    // a whole layer of chunks before it can stop at the deadline.
    struct RegionSpectralPace {
        std::mutex mutex;
        RegionSpectralKey key;
        std::size_t channels_done = 0;
        std::int64_t reads = 0;
        // The state of each stokes, by stokes index. All of them are for `key`.
        std::map<int, RegionSpectralState> stokes;

        // Starts every stokes of the region again, for the key `other`
        void Reset(const RegionSpectralKey& other);
    };

    // Held only while finding or adding a region's pace. A pace is never erased, so a reference to it stays valid.
    std::mutex _region_spectral_mutex;
    // The pace of each region, by region id
    std::map<int, RegionSpectralPace> _region_spectral;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
