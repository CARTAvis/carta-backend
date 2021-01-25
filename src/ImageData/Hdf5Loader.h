/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_

#include <unordered_map>

#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "../Frame.h"
#include "../Util.h"
#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Hdf5Attributes.h"

namespace carta {

class Hdf5Loader : public FileLoader {
public:
    Hdf5Loader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

    bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) override;
    bool UseRegionSpectralData(const IPos& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask, const IPos& origin,
        std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) override;

private:
    std::string _hdu;
    std::unique_ptr<CartaHdf5Image> _image;
    std::unique_ptr<casacore::HDF5Lattice<float>> _swizzled_image;
    std::map<FileInfo::RegionStatsId, FileInfo::RegionSpectralStats> _region_stats;
    Frame* _frame;

    std::string DataSetToString(FileInfo::Data ds) const;

    template <typename T>
    const IPos GetStatsDataShapeTyped(FileInfo::Data ds);
    template <typename S, typename D>
    casacore::ArrayBase* GetStatsDataTyped(FileInfo::Data ds);

    const IPos GetStatsDataShape(FileInfo::Data ds) override;
    casacore::ArrayBase* GetStatsData(FileInfo::Data ds) override;

    casacore::Lattice<float>* LoadSwizzledData();
};

} // namespace carta

#include "Hdf5Loader.tcc"

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
