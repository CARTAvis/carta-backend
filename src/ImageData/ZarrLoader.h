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

#include "FileLoader.h"
#include "Util/Image.h"

namespace carta {

// Whether a spectrum whose column is `chunks_x` by `chunks_y` by `chunks_z` chunks, each decoding to
// `chunk_bytes`, keeps them in a shared cache of `cache_bytes`: whenever they fit as they decode, so that a
// cursor moving within them reads them from the cache.
bool SpectrumKeepsItsChunks(
    std::uint64_t chunks_x, std::uint64_t chunks_y, std::uint64_t chunks_z, std::uint64_t chunk_bytes, std::size_t cache_bytes);

class ZarrLoader : public FileLoader {
public:
    explicit ZarrLoader(const std::string& filename);

    // Reads the spectrum of one pixel with carta-zarr; several pixels are refused. `progress` is told how many
    // channels are final as the read goes, and stops the read by returning false.
    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const SpectralReadProgress& progress) override;
    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress, const SpectralReadWanted& still_wanted) override;

protected:
    void AllocateImage(const std::string& hdu) override;

    // How long one GetRegionSpectralData call reads before it hands back what is finished, and how many
    // channels carta-zarr finishes at a time; zero lets it choose.
    //
    // As long as the region handler waits between sending partial results. A shorter call would not stop a
    // profile sooner, since the handler's checks reach a call as it reads, nor let other regions read sooner,
    // since they do not wait for it; and read in calls of 50 ms, profiles took up to twice as long.
    //
    // Protected so that a test can make a call pause after every run, or never.
    std::chrono::milliseconds _region_spectral_time{TARGET_PARTIAL_REGION_TIME};
    std::uint32_t _region_spectral_channels_per_emit = 0;

    // What a profile is of: the region and the channel range. Another region or range starts the profile
    // again. The mask is kept as a hash of its pixels and their count rather than a copy, which for a large
    // region would be tens of MB kept for as long as the image is open. Protected so that a test can tell
    // two masks' keys apart.
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
    // progress is 1, and checks between calls that the region and the request are unchanged.
    struct RegionSpectralState {
        std::size_t channels_done = 0;
        std::map<CARTA::StatsType, std::vector<double>> stats;
        // Each thread's last read of this stokes: its index, counted in the region's reads, and the channels the
        // thread was handed then. A profile is read on one thread from start to end.
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
    // - A call's progress is the fewest channels this thread was handed, of this stokes and of every stokes
    //   the thread read since it last read this one. Those are the stokes this one is combined with, so a
    //   progress of 1 means all of them are finished, whatever another profile of the region read meanwhile.
    //
    // Held for a whole call, so that one region's profiles take turns. Another region's do not wait for it:
    // a region covering a large image reads a whole layer of chunks before it can stop at the deadline.
    struct RegionSpectralPace {
        std::mutex mutex;
        RegionSpectralKey key;
        std::size_t channels_done = 0;
        std::int64_t reads = 0;
        // By stokes, all of the region's key
        std::map<int, RegionSpectralState> stokes;

        // Starts every stokes of the region again, for another key
        void Reset(const RegionSpectralKey& other);
    };

    // Held only to find or add a region's pace. A pace is never erased, so it stays where it is once found.
    std::mutex _region_spectral_mutex;
    // By region id
    std::map<int, RegionSpectralPace> _region_spectral;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
