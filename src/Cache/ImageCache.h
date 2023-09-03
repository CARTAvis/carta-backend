/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_IMAGECACHE_H_
#define CARTA_BACKEND__FRAME_IMAGECACHE_H_

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

enum ImageCacheType { Cube, Channel };

class ImageCache {
public:
    ImageCache(ImageCacheType type);
    virtual ~ImageCache() = default;

    virtual float* AllocateData(int stokes, size_t data_size);
    virtual float* GetChannelImageCache(int z, int stokes, size_t width, size_t height);

    virtual bool LoadCachedPointSpectralData(
        std::vector<float>& profile, int stokes, PointXy point, size_t width, size_t height, size_t depth);
    virtual bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, size_t width, size_t height,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
        std::map<CARTA::StatsType, std::vector<double>>& profiles);
    virtual float GetValue(int x, int y, int z, int stokes, size_t width, size_t height);

    virtual bool DataExist() const;
    virtual bool DataExist(int stokes) const;

    ImageCacheType Type() const {
        return _type;
    }

private:
    enum ImageCacheType _type;
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_IMAGECACHE_H_
