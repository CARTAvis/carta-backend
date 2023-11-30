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

// Global variable
extern float FULL_IMAGE_CACHE_SIZE_AVAILABLE; // MB
extern std::mutex FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX;

namespace carta {

enum ImageCacheType { Cube, Channel };

class ImageCache {
public:
    ImageCache(ImageCacheType type, size_t width, size_t height, size_t depth, size_t num_stokes);
    virtual ~ImageCache() = default;

    virtual float* AllocateData(int stokes, size_t data_size) = 0;
    virtual float* GetChannelData(int z, int stokes) = 0;
    virtual float GetValue(int x, int y, int z, int stokes) = 0;

    virtual bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) = 0;
    virtual bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) = 0;
    virtual bool DataExist(int stokes) const = 0;
    virtual bool CachedChannelDataAvailable() const = 0;

    virtual void ValidateChannelImageCache() = 0;
    virtual void InvalidateChannelImageCache() = 0;

    void LoadCachedPointSpatialData(std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes);

    ImageCacheType Type() const {
        return _type;
    }

    int& StokesI() {
        return _stokes_i;
    }
    int& StokesQ() {
        return _stokes_q;
    }
    int& StokesU() {
        return _stokes_u;
    }
    int& StokesV() {
        return _stokes_v;
    }
    double& BeamArea() {
        return _beam_area;
    }

    static void AssignFullImageCacheSizeAvailable(int& full_image_cache_size_available, std::string& msg);
    static float ImageMemorySize(size_t width, size_t height, size_t depth, size_t num_stokes); // MB

protected:
    enum ImageCacheType _type;

    // Cube image cache size
    size_t _width;
    size_t _height;
    size_t _depth;
    size_t _num_stokes;

    int _stokes_i; // stokes type "I" index
    int _stokes_q; // stokes type "Q" index
    int _stokes_u; // stokes type "U" index
    int _stokes_v; // stokes type "V" index
    double _beam_area;
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_IMAGECACHE_H_
