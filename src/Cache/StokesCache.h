/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_STOKESCACHE_H_
#define CARTA_SRC_CACHE_STOKESCACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class StokesCache : public ImageCache {
public:
    StokesCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);
    ~StokesCache() override;

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
    bool FillStokesCache(std::unique_ptr<float[]>& stokes_data, int stokes);

    double _beam_area;
    std::unique_ptr<float[]> _stokes_data;
    bool _stokes_image_cache_valid; // Cached image data is valid for the current stokes
};

} // namespace carta

#endif // CARTA_SRC_CACHE_STOKESCACHE_H_
