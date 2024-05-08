/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_FULLIMAGECACHE_H_
#define CARTA_SRC_CACHE_FULLIMAGECACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class FullImageCache : public ImageCache {
public:
    FullImageCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);
    ~FullImageCache() override;

    void UpdateValidity(bool stokes_changed) override;
    float* GetChannelData(int z, int stokes) override;
    float DoGetValue(int x, int y, int z, int stokes) override;

    bool LoadPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) override;
    bool LoadRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) override;

private:
    inline float GetValue(int x, int y, int z, int stokes) override;
    bool ChannelDataAvailable(int z, int stokes) override;
    bool UpdateChannelCache(int z, int stokes) override;
    bool FillFullImageCache(std::map<int, std::unique_ptr<float[]>>& stokes_data);

    int _stokes_i; // stokes "I" index
    int _stokes_q; // stokes "Q" index
    int _stokes_u; // stokes "U" index
    int _stokes_v; // stokes "V" index
    double _beam_area;
    int _current_computed_stokes_channel;

    // Map of the cube image cache, key is the stokes index (I, Q, U, or V). For computed stokes, it only caches image data per channel.
    std::map<int, std::unique_ptr<float[]>> _stokes_data;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_FULLIMAGECACHE_H_
