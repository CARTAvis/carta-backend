/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_CUBEIMAGECACHE_H_
#define CARTA_SRC_CACHE_CUBEIMAGECACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class CubeImageCache : public ImageCache {
public:
    CubeImageCache(std::shared_ptr<FileLoader> loader, std::shared_ptr<ImageState> image_state, std::mutex& image_mutex);
    ~CubeImageCache() override;

    float* GetChannelData(int z, int stokes) override;
    inline float GetValue(int x, int y, int z, int stokes) const override;

    bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) override;
    bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) override;
    bool CachedChannelDataAvailable(int z, int stokes) const override;

    bool UpdateChannelImageCache(int z, int stokes) override;
    void SetImageChannels(int z, int stokes) override;

private:
    bool FillCubeImageCache(std::unique_ptr<float[]>& stokes_data, int stokes);

    double _beam_area;
    std::unique_ptr<float[]> _stokes_data;
    bool _stokes_image_cache_valid; // Cached image data is valid for the current stokes
};

} // namespace carta

#endif // CARTA_SRC_CACHE_CUBEIMAGECACHE_H_
