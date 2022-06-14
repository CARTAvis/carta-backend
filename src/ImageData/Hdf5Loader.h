/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_

#include <regex>
#include <unordered_map>
#include <unordered_set>

#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "Frame/Frame.h"

#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Hdf5Attributes.h"

namespace carta {

class Hdf5Loader : public FileLoader {
public:
    Hdf5Loader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;

    bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) override;

    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress) override;
    bool GetDownsampledRasterData(
        std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex) override;
    bool GetChunk(std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes,
        std::mutex& image_mutex) override;

    bool HasMip(int mip) const override;
    bool UseTileCache() const override;

private:
    std::string _hdu;
    std::unique_ptr<casacore::HDF5Lattice<float>> _swizzled_image;
    std::unordered_map<int, std::unique_ptr<casacore::HDF5Lattice<float>>> _mipmaps;

    std::map<FileInfo::RegionStatsId, FileInfo::RegionSpectralStats> _region_stats;

    H5D_layout_t _layout;

    std::string DataSetToString(FileInfo::Data ds) const;
    bool HasData(std::string ds_name) const;

    template <typename T>
    const casacore::IPosition GetStatsDataShapeTyped(FileInfo::Data ds);
    template <typename S, typename D>
    std::unique_ptr<casacore::ArrayBase> GetStatsDataTyped(FileInfo::Data ds);

    const casacore::IPosition GetStatsDataShape(FileInfo::Data ds) override;
    std::unique_ptr<casacore::ArrayBase> GetStatsData(FileInfo::Data ds) override;

    casacore::Lattice<float>* LoadSwizzledData();
    casacore::Lattice<float>* LoadMipMapData(int mip);
};

} // namespace carta

#include "Hdf5Loader.tcc"

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
