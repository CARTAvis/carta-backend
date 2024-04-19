/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_IMAGECACHE_H_
#define CARTA_SRC_CACHE_IMAGECACHE_H_

#include "ImageData/FileLoader.h"
#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class Frame;

class ImageCache {
public:
    ImageCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);
    virtual ~ImageCache() = default;

    static std::unique_ptr<ImageCache> GetImageCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);
    static void SetFullImageCacheSize(int& full_image_cache_size_available, std::string& msg);
    static float ImageMemorySize(size_t width, size_t height, size_t depth, size_t num_stokes); // MB

    StokesSlicer GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes);
    casacore::IPosition OriginalImageShape() const;
    bool GetSlicerData(const StokesSlicer& stokes_slicer, float* data);
    bool GetStokesTypeIndex(const string& coordinate, int& stokes_index, bool mute_err_msg);
    bool TileCacheAvailable();

    virtual float* GetChannelData(int z, int stokes) = 0;
    virtual float GetValue(int x, int y, int z, int stokes) const = 0;

    virtual bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) = 0;
    virtual bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) = 0;
    virtual bool CachedChannelDataAvailable(int z, int stokes) const = 0;

    virtual bool UpdateChannelCache(int z, int stokes) = 0;
    virtual void SetImageChannels(int z, int stokes) = 0;

    void LoadCachedPointSpatialData(
        std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes) const;
    bool IsValid() const;

protected:
    void DoStatisticsCalculations(const AxisRange& z_range, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, double beam_area, const std::function<float(size_t idx)>& get_value,
        std::map<CARTA::StatsType, std::vector<double>>& profiles);

    static float _full_image_cache_size_available; // MB
    static std::mutex _full_image_cache_size_available_mutex;

    Frame* _frame;
    std::shared_ptr<FileLoader> _loader;
    std::mutex& _image_mutex; // Reference of the image mutex for the file loader
    bool _valid;
    float _image_memory_size;

    // Cube image cache size
    size_t _width;
    size_t _height;
    size_t _depth;
    size_t _num_stokes;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_IMAGECACHE_H_
