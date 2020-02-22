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
    bool UseRegionSpectralData(const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, int profile_index, int stokes,
        const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, IPos origin, std::mutex& image_mutex,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)>& partial_results_callback) override;
    void SetFramePtr(Frame* frame) override;

private:
    std::string _filename;
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
