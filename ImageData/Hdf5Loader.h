#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_

#include <unordered_map>

#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Hdf5Attributes.h"
#include "../Util.h"

namespace carta {

class Hdf5Loader : public FileLoader {
public:
    Hdf5Loader(const std::string& filename);
    void OpenFile(const std::string& hdu, const CARTA::FileInfoExtended* info) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y) override;
    bool CanUseSiwzzledData(const casacore::ArrayLattice<casacore::Bool>* mask) override;
    bool GetRegionSpectralData(
        int stokes, int region_id, const casacore::ArrayLattice<casacore::Bool>* mask, IPos origin,
        std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)> cb) override;

protected:
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) override;

private:
    std::string _filename;
    std::string _hdu;
    std::unique_ptr<CartaHdf5Image> _image;
    std::unique_ptr<casacore::HDF5Lattice<float>> _swizzled_image;
    std::map<FileInfo::RegionStatsId, FileInfo::RegionSpectralStats> _region_stats;

    std::string DataSetToString(FileInfo::Data ds) const;

    template <typename T>
    const IPos GetStatsDataShapeTyped(FileInfo::Data ds);
    template <typename S, typename D>
    casacore::ArrayBase* GetStatsDataTyped(FileInfo::Data ds);

    const IPos GetStatsDataShape(FileInfo::Data ds) override;
    casacore::ArrayBase* GetStatsData(FileInfo::Data ds) override;

    casacore::Lattice<float>* LoadSwizzledData(FileInfo::Data ds);
};

Hdf5Loader::Hdf5Loader(const std::string& filename) : _filename(filename), _hdu("0") {}

void Hdf5Loader::OpenFile(const std::string& hdu, const CARTA::FileInfoExtended* info) {
    // Open hdf5 image with specified hdu
    _image = std::unique_ptr<CartaHdf5Image>(new CartaHdf5Image(_filename, DataSetToString(FileInfo::Data::Image), hdu, info));
    _hdu = hdu;
    _connected = true;

    // We need this immediately because dataSetToString uses it to find the name of the swizzled dataset
    _num_dims = _image->shape().size();

    // Load swizzled image lattice
    if (HasData(FileInfo::Data::SWIZZLED)) {
        _swizzled_image = std::unique_ptr<casacore::HDF5Lattice<float>>(
            new casacore::HDF5Lattice<float>(_filename, DataSetToString(FileInfo::Data::SWIZZLED), hdu));
    }
}

// We assume that the main image dataset is always loaded and therefore available.
// For everything else, we refer back to the file.
bool Hdf5Loader::HasData(FileInfo::Data ds) const {
    switch (ds) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            auto group_ptr = _image->Group();
            std::string data(DataSetToString(ds));
            if (data.empty()) {
                return false;
            }
            return casacore::HDF5Group::exists(*group_ptr, data);
    }
}

// TODO: when we fix the typing issue, this should probably return any dataset again, for consistency.
typename Hdf5Loader::ImageRef Hdf5Loader::LoadData(FileInfo::Data ds) {
    // returns an ImageInterface*
    switch (ds) {
        case FileInfo::Data::Image:
            return _image.get();
        case FileInfo::Data::XY:
            if (_num_dims == 2) {
                return _image.get();
            }
        case FileInfo::Data::XYZ:
            if (_num_dims == 3) {
                return _image.get();
            }
        case FileInfo::Data::XYZW:
            if (_num_dims == 4) {
                return _image.get();
            }
        default:
            break;
    }
    throw casacore::HDF5Error("Unable to load dataset " + DataSetToString(ds) + ".");
}

casacore::Lattice<float>* Hdf5Loader::LoadSwizzledData(FileInfo::Data ds) {
    // swizzled data returns a Lattice
    switch (ds) {
        case FileInfo::Data::SWIZZLED:
            if (_swizzled_image) {
                return _swizzled_image.get();
            }
        case FileInfo::Data::ZYX:
            if (_num_dims == 3 && _swizzled_image) {
                return _swizzled_image.get();
            }
        case FileInfo::Data::ZYXW:
            if (_num_dims == 4 && _swizzled_image) {
                return _swizzled_image.get();
            }
        default:
            break;
    }

    throw casacore::HDF5Error("Unable to load swizzled dataset " + DataSetToString(ds) + ".");
}

bool Hdf5Loader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    if (_image == nullptr) {
        return false;
    } else {
        return _image->getMaskSlice(mask, slicer);
    }
}

// This is necessary on some systems where the compiler
// cannot infer the implicit cast to the enum class's
// underlying type (e.g. MacOS, CentOS7).
struct EnumClassHash {
    template <typename T>
    using utype_t = typename std::underlying_type<T>::type;

    template <typename T>
    utype_t<T> operator()(T t) const {
        return static_cast<utype_t<T>>(t);
    }
};

std::string Hdf5Loader::DataSetToString(FileInfo::Data ds) const {
    static std::unordered_map<FileInfo::Data, std::string, EnumClassHash> um = {
        {FileInfo::Data::Image, "DATA"},
        {FileInfo::Data::YX, "SwizzledData/YX"},
        {FileInfo::Data::ZYX, "SwizzledData/ZYX"},
        {FileInfo::Data::ZYXW, "SwizzledData/ZYXW"},
        {FileInfo::Data::STATS, "Statistics"},
        {FileInfo::Data::STATS_2D, "Statistics/XY"},
        {FileInfo::Data::STATS_2D_MIN, "Statistics/XY/MIN"},
        {FileInfo::Data::STATS_2D_MAX, "Statistics/XY/MAX"},
        {FileInfo::Data::STATS_2D_MEAN, "Statistics/XY/MEAN"},
        {FileInfo::Data::STATS_2D_NANS, "Statistics/XY/NAN_COUNT"},
        {FileInfo::Data::STATS_2D_HIST, "Statistics/XY/HISTOGRAM"},
        {FileInfo::Data::STATS_2D_PERCENT, "Statistics/XY/PERCENTILES"},
        {FileInfo::Data::STATS_3D, "Statistics/XYZ"},
        {FileInfo::Data::STATS_3D_MIN, "Statistics/XYZ/MIN"},
        {FileInfo::Data::STATS_3D_MAX, "Statistics/XYZ/MAX"},
        {FileInfo::Data::STATS_3D_MEAN, "Statistics/XYZ/MEAN"},
        {FileInfo::Data::STATS_3D_NANS, "Statistics/XYZ/NAN_COUNT"},
        {FileInfo::Data::STATS_3D_HIST, "Statistics/XYZ/HISTOGRAM"},
        {FileInfo::Data::STATS_3D_PERCENT, "Statistics/XYZ/PERCENTILES"},
        {FileInfo::Data::RANKS, "PERCENTILE_RANKS"},
    };

    switch (ds) {
        case FileInfo::Data::XY:
            return _num_dims == 2 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZ:
            return _num_dims == 3 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZW:
            return _num_dims == 4 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::SWIZZLED:
            switch (_num_dims) {
                case 2:
                    return DataSetToString(FileInfo::Data::YX);
                case 3:
                    return DataSetToString(FileInfo::Data::ZYX);
                case 4:
                    return DataSetToString(FileInfo::Data::ZYXW);
                default:
                    return "";
            }
        default:
            return (um.find(ds) != um.end()) ? um[ds] : "";
    }
}

bool Hdf5Loader::GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) {
    if (_image == nullptr) {
        return false;
    } else {
        coord_sys = _image->coordinates();
        return true;
    }
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
const Hdf5Loader::IPos Hdf5Loader::GetStatsDataShape(FileInfo::Data ds) {
    auto data_type = casacore::HDF5DataSet::getDataType(_image->Group()->getHid(), DataSetToString(ds));

    switch (data_type) {
        case casacore::TpInt: {
            return GetStatsDataShapeTyped<casacore::Int>(ds);
        }
        case casacore::TpInt64: {
            return GetStatsDataShapeTyped<casacore::Int64>(ds);
        }
        case casacore::TpFloat: {
            return GetStatsDataShapeTyped<casacore::Float>(ds);
        }
        case casacore::TpDouble: {
            return GetStatsDataShapeTyped<casacore::Double>(ds);
        }
        default: { throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype."); }
    }
}

template <typename T>
const Hdf5Loader::IPos Hdf5Loader::GetStatsDataShapeTyped(FileInfo::Data ds) {
    casacore::HDF5DataSet data_set(*(_image->Group()), DataSetToString(ds), (const T*)0);
    return data_set.shape();
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
casacore::ArrayBase* Hdf5Loader::GetStatsData(FileInfo::Data ds) {
    auto data_type = casacore::HDF5DataSet::getDataType(_image->Group()->getHid(), DataSetToString(ds));

    switch (data_type) {
        case casacore::TpInt: {
            return GetStatsDataTyped<casacore::Int, casacore::Int64>(ds);
        }
        case casacore::TpInt64: {
            return GetStatsDataTyped<casacore::Int64, casacore::Int64>(ds);
        }
        case casacore::TpFloat: {
            return GetStatsDataTyped<casacore::Float, casacore::Float>(ds);
        }
        case casacore::TpDouble: {
            return GetStatsDataTyped<casacore::Double, casacore::Float>(ds);
        }
        default: { throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype."); }
    }
}

// TODO: We need to use the C API to read scalar datasets for now, but we should patch casacore to handle them correctly.
template <typename S, typename D>
casacore::ArrayBase* Hdf5Loader::GetStatsDataTyped(FileInfo::Data ds) {
    casacore::HDF5DataSet data_set(*(_image->Group()), DataSetToString(ds), (const S*)0);

    if (data_set.shape().size() == 0) {
        // Scalar dataset hackaround
        D value;
        casacore::HDF5DataType data_type((D*)0);
        H5Dread(data_set.getHid(), data_type.getHidMem(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &value);
        casacore::ArrayBase* scalar = new casacore::Array<D>(IPos(1, 1), value);
        return scalar;
    }

    casacore::ArrayBase* data = new casacore::Array<D>();
    data_set.get(casacore::Slicer(IPos(data_set.shape().size(), 0), data_set.shape()), *data);
    return data;
}

bool Hdf5Loader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y) {
    bool data_ok(false);
    if (HasData(FileInfo::Data::SWIZZLED)) {
        casacore::Slicer slicer;
        if (_num_dims == 4) {
            slicer = casacore::Slicer(IPos(4, 0, cursor_y, cursor_x, stokes), IPos(4, _num_channels, count_y, count_x, 1));
        } else if (_num_dims == 3) {
            slicer = casacore::Slicer(IPos(3, 0, cursor_y, cursor_x), IPos(3, _num_channels, count_y, count_x));
        }

        data.resize(_num_channels * count_y * count_x);
        casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        try {
            LoadSwizzledData(FileInfo::Data::SWIZZLED)->doGetSlice(tmp, slicer);
            data_ok = true;
        } catch (casacore::AipsError& err) {
            std::cerr << "Could not load cursor spectral data from swizzled HDF5 dataset. AIPS ERROR: " << err.getMesg() << std::endl;
        }
    }

    return data_ok;
}

bool Hdf5Loader::CanUseSiwzzledData(const casacore::ArrayLattice<casacore::Bool>* mask) {
    if (!HasData(FileInfo::Data::SWIZZLED)) {
        return false;
    }

    int num_y = mask->shape()(0);
    int num_x = mask->shape()(1);
    int num_z = _num_channels;

    // Using the normal dataset may be faster if the region is wider than it is deep.
    // This is an initial estimate; we need to examine casacore's algorithm in more detail.
    if (num_y * num_z < num_x) {
        return false;
    }

    return true;
}

bool Hdf5Loader::GetRegionSpectralData(
    int stokes, int region_id, const casacore::ArrayLattice<casacore::Bool>* mask, IPos origin,
    std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)> cb) {
    if (!HasData(FileInfo::Data::SWIZZLED)) {
        return false;
    }

    int num_y = mask->shape()(0);
    int num_x = mask->shape()(1);
    int num_z = _num_channels;

    bool recalculate(false);
    auto region_stats_id = FileInfo::RegionStatsId(region_id, stokes);

    if (_region_stats.find(region_stats_id) == _region_stats.end()) { // region stats never calculated
        _region_stats.emplace(
            std::piecewise_construct, std::forward_as_tuple(region_id, stokes), std::forward_as_tuple(origin, mask->shape(), num_z));
        recalculate = true;
    } else if (!_region_stats[region_stats_id].IsValid(origin, mask->shape())) { // region stats expired
        // TODO: This check "_region_stats[region_stats_id].IsValid(origin, mask->shape())"
        //       seems not work for the rotation of an ellipse region
        _region_stats[region_stats_id].origin = origin;
        _region_stats[region_stats_id].shape = mask->shape();
        recalculate = true;
    }

    if (recalculate) {
        int x_min = origin(0);
        int x_max = x_min + num_x;
        int y_min = origin(1);
        int y_max = y_min + num_y;

        auto& stats = _region_stats[region_stats_id].stats;

        auto& num_pixels = stats[CARTA::StatsType::NumPixels];
        auto& nan_count = stats[CARTA::StatsType::NanCount];
        auto& sum = stats[CARTA::StatsType::Sum];
        auto& mean = stats[CARTA::StatsType::Mean];
        auto& rms = stats[CARTA::StatsType::RMS];
        auto& sigma = stats[CARTA::StatsType::Sigma];
        auto& sum_sq = stats[CARTA::StatsType::SumSq];
        auto& min = stats[CARTA::StatsType::Min];
        auto& max = stats[CARTA::StatsType::Max];

        std::vector<float> slice_data;

        // Set initial values of stats which will be incremented (we may have expired region data)
        for (size_t z = 0; z < num_z; z++) {
            min[z] = FLT_MAX;
            max[z] = FLT_MIN;
            num_pixels[z] = 0;
            nan_count[z] = 0;
            sum[z] = 0;
            sum_sq[z] = 0;
        }

        RegionState region_state = _region_states[region_id];
        std::map<CARTA::StatsType, std::vector<double>>* stats_values;
        float progress;

        // start the timer
        auto tStart = std::chrono::high_resolution_clock::now();
        int time_step = 0;

        // Load each X slice of the swizzled region bounding box and update Z stats incrementally
        for (size_t x = 0; x < num_x; x++) {
            if (!IsConnected()) {
                std::cerr << "[Region " << region_id << "] closing image, exit zprofile (statistics) before complete" << std::endl;
                return false;
            }
            if (!IsSameRegionState(region_id, region_state)) {
                std::cerr << "[Region " << region_id << "] region state changed, exit zprofile (statistics) before complete" << std::endl;
                return false;
            }

            bool have_spectral_data = GetCursorSpectralData(slice_data, stokes, x + x_min, 1, y_min, num_y);
            if (!have_spectral_data) {
                return false;
            }

            for (size_t y = 0; y < num_y; y++) {
                // skip all Z values for masked pixels
                if (!mask->getAt(IPos(2, x, y))) {
                    continue;
                }
                for (size_t z = 0; z < num_z; z++) {
                    double v = slice_data[y * num_z + z];

                    if (std::isfinite(v)) {
                        num_pixels[z] += 1;

                        sum[z] += v;
                        sum_sq[z] += v * v;

                        if (v < min[z]) {
                            min[z] = v;
                        } else if (v > max[z]) {
                            max[z] = v;
                        }

                    } else {
                        nan_count[z] += 1;
                    }
                }
            }

            // get the time elapse for this step
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

            // check whether to send partial results to the frontend
            if (dt > time_step * TARGET_DELTA_TIME) {
                float mean_sq;

                // Calculate partial stats
                for (size_t z = 0; z < num_z; z++) {
                    if (num_pixels[z]) {
                        mean[z] = sum[z] / num_pixels[z];

                        mean_sq = sum_sq[z] / num_pixels[z];
                        rms[z] = sqrt(mean_sq);
                        sigma[z] = sqrt(mean_sq - (mean[z] * mean[z]));
                    } else {
                        // if there are no valid values, set all stats to NaN except the value and NaN counts
                        for (auto& kv : stats) {
                            switch (kv.first) {
                                case CARTA::StatsType::NanCount:
                                case CARTA::StatsType::NumPixels:
                                    break;
                                default:
                                    kv.second[z] = NAN;
                                    break;
                            }
                        }
                    }
                }

                time_step++;
                progress = x / num_x;
                stats_values = &_region_stats[region_stats_id].stats;

                // send partial result by the callback function
                cb(stats_values, progress);
            }
        }

        float mean_sq;

        // Calculate final stats
        for (size_t z = 0; z < num_z; z++) {
            if (num_pixels[z]) {
                mean[z] = sum[z] / num_pixels[z];

                mean_sq = sum_sq[z] / num_pixels[z];
                rms[z] = sqrt(mean_sq);
                sigma[z] = sqrt(mean_sq - (mean[z] * mean[z]));
            } else {
                // if there are no valid values, set all stats to NaN except the value and NaN counts
                for (auto& kv : stats) {
                    switch (kv.first) {
                        case CARTA::StatsType::NanCount:
                        case CARTA::StatsType::NumPixels:
                            break;
                        default:
                            kv.second[z] = NAN;
                            break;
                    }
                }
            }
        }
    }

    std::map<CARTA::StatsType, std::vector<double>>* stats_values =
        &_region_stats[region_stats_id].stats;

    // send final result by the callback function
    cb(stats_values, 1.0);

    return true;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
