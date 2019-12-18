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

Hdf5Loader::Hdf5Loader(const std::string& filename) : _filename(filename), _hdu("0") {}

void Hdf5Loader::OpenFile(const std::string& hdu) {
    // Open hdf5 image with specified hdu
    if (!_image || (hdu != _hdu)) {
        _image.reset(new CartaHdf5Image(_filename, DataSetToString(FileInfo::Data::Image), hdu));
        _hdu = hdu;

        // We need this immediately because dataSetToString uses it to find the name of the swizzled dataset
        _num_dims = _image->shape().size();

        // Load swizzled image lattice
        if (HasData(FileInfo::Data::SWIZZLED)) {
            _swizzled_image = std::unique_ptr<casacore::HDF5Lattice<float>>(
                new casacore::HDF5Lattice<float>(casacore::CountedPtr<casacore::HDF5File>(new casacore::HDF5File(_filename)),
                    DataSetToString(FileInfo::Data::SWIZZLED), hdu));
        }
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
typename Hdf5Loader::ImageRef Hdf5Loader::GetImage() {
    // returns opened image as ImageInterface*
    return _image.get();
}

casacore::Lattice<float>* Hdf5Loader::LoadSwizzledData() {
    // swizzled data returns a Lattice
    return _swizzled_image.get();
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
        {FileInfo::Data::STATS_2D_SUM, "Statistics/XY/SUM"},
        {FileInfo::Data::STATS_2D_SUMSQ, "Statistics/XY/SUM_SQ"},
        {FileInfo::Data::STATS_2D_NANS, "Statistics/XY/NAN_COUNT"},
        {FileInfo::Data::STATS_2D_HIST, "Statistics/XY/HISTOGRAM"},
        {FileInfo::Data::STATS_2D_PERCENT, "Statistics/XY/PERCENTILES"},
        {FileInfo::Data::STATS_3D, "Statistics/XYZ"},
        {FileInfo::Data::STATS_3D_MIN, "Statistics/XYZ/MIN"},
        {FileInfo::Data::STATS_3D_MAX, "Statistics/XYZ/MAX"},
        {FileInfo::Data::STATS_3D_SUM, "Statistics/XYZ/SUM"},
        {FileInfo::Data::STATS_3D_SUMSQ, "Statistics/XYZ/SUM_SQ"},
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

bool Hdf5Loader::GetCursorSpectralData(
    std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) {
    bool data_ok(false);
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (has_swizzled) {
        casacore::Slicer slicer;
        if (_num_dims == 4) {
            slicer = casacore::Slicer(IPos(4, 0, cursor_y, cursor_x, stokes), IPos(4, _num_channels, count_y, count_x, 1));
        } else if (_num_dims == 3) {
            slicer = casacore::Slicer(IPos(3, 0, cursor_y, cursor_x), IPos(3, _num_channels, count_y, count_x));
        }

        data.resize(_num_channels * count_y * count_x);
        casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        std::lock_guard<std::mutex> lguard(image_mutex);
        try {
            LoadSwizzledData()->doGetSlice(tmp, slicer);
            data_ok = true;
        } catch (casacore::AipsError& err) {
            std::cerr << "Could not load cursor spectral data from swizzled HDF5 dataset. AIPS ERROR: " << err.getMesg() << std::endl;
        }
    }
    return data_ok;
}

bool Hdf5Loader::UseRegionSpectralData(const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, std::mutex& image_mutex) {
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return false;
    }

    int num_x = mask->shape()(0);
    int num_y = mask->shape()(1);
    int num_z = _num_channels;

    // Using the normal dataset may be faster if the region is wider than it is deep.
    // This is an initial estimate; we need to examine casacore's algorithm in more detail.
    if (num_y * num_z < num_x) {
        return false;
    }

    return true;
}

bool Hdf5Loader::GetRegionSpectralData(int region_id, int config_stokes, int profile_stokes,
    const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, IPos origin, std::mutex& image_mutex,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)>& partial_results_callback) {
    // config_stokes is the stokes value in the spectral config, could be -1 for CURRENT_STOKES; used to retrieve config from region
    // profile_stokes is the stokes index to use for slicing image data
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return false;
    }

    int num_x = mask->shape()(0);
    int num_y = mask->shape()(1);
    int num_z = _num_channels;

    bool recalculate(false);
    auto region_stats_id = FileInfo::RegionStatsId(region_id, profile_stokes);

    if (_region_stats.find(region_stats_id) == _region_stats.end()) { // region stats never calculated
        _region_stats.emplace(std::piecewise_construct, std::forward_as_tuple(region_id, profile_stokes),
            std::forward_as_tuple(origin, mask->shape(), num_z));
        recalculate = true;
    } else if (!_region_stats[region_stats_id].IsValid(origin, mask->shape())) { // region stats expired
        _region_stats[region_stats_id].origin = origin;
        _region_stats[region_stats_id].shape = mask->shape();
        _region_stats[region_stats_id].completed = false;
        _region_stats[region_stats_id].latest_x = 0;
        recalculate = true;
    } else if ( // region stats is not expired but previous calculation is not completed
        _region_stats[region_stats_id].IsValid(origin, mask->shape()) && !_region_stats[region_stats_id].IsCompleted()) {
        // resume the calculation
        recalculate = true;
    }

    if (recalculate) {
        int x_min = origin(0);
        int y_min = origin(1);

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

        // get the start of X
        size_t x_start = _region_stats[region_stats_id].latest_x;

        if (x_start == 0) {
            // Set initial values of stats which will be incremented (we may have expired region data)
            for (size_t z = 0; z < num_z; z++) {
                min[z] = FLT_MAX;
                max[z] = FLT_MIN;
                num_pixels[z] = 0;
                nan_count[z] = 0;
                sum[z] = 0;
                sum_sq[z] = 0;
            }
        }

        // get a copy of current region state
        RegionState region_state;
        if (!_frame->GetRegionState(region_id, region_state)) {
            return false;
        }

        // get a copy of current region configs
        SpectralConfig config_stats;
        if (!_frame->GetRegionSpectralConfig(region_id, config_stokes, config_stats)) {
            return false;
        }

        std::map<CARTA::StatsType, std::vector<double>>* stats_values;
        float progress;

        // start the timer
        auto t_start = std::chrono::high_resolution_clock::now();
        auto t_latest = t_start;

        // Lambda to calculate mean, sigma and RMS
        auto calculate_stats = [&]() {
            double sum_z, sum_sq_z;
            uint64_t num_pixels_z;

            for (size_t z = 0; z < num_z; z++) {
                if (num_pixels[z]) {
                    sum_z = sum[z];
                    sum_sq_z = sum_sq[z];
                    num_pixels_z = num_pixels[z];

                    mean[z] = sum_z / num_pixels_z;
                    rms[z] = sqrt(sum_sq_z / num_pixels_z);
                    sigma[z] = sqrt((sum_sq_z - (sum_z * sum_z / num_pixels_z)) / (num_pixels_z - 1));
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
        };

        // Load each X slice of the swizzled region bounding box and update Z stats incrementally
        for (size_t x = x_start; x < num_x; x++) {
            // check if frontend's requirements changed
            if (_frame != nullptr && _frame->Interrupt(region_id, profile_stokes, region_state, config_stats, true)) {
                // remember the latest x step
                _region_stats[region_stats_id].latest_x = x;
                return false;
            }

            bool have_spectral_data = GetCursorSpectralData(slice_data, profile_stokes, x + x_min, 1, y_min, num_y, image_mutex);
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
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(t_end - t_latest).count();

            progress = (float)x / num_x;
            // check whether to send partial results to the frontend
            if (dt > TARGET_PARTIAL_REGION_TIME && x < num_x) {
                // Calculate partial stats
                calculate_stats();

                stats_values = &_region_stats[region_stats_id].stats;

                t_latest = std::chrono::high_resolution_clock::now();
                // send partial result by the callback function
                partial_results_callback(stats_values, progress);
            }
        }

        // Calculate final stats
        calculate_stats();

        // the stats calculation is completed
        _region_stats[region_stats_id].completed = true;
    }

    std::map<CARTA::StatsType, std::vector<double>>* stats_values = &_region_stats[region_stats_id].stats;

    // send final result by the callback function
    partial_results_callback(stats_values, 1.0f);

    return true;
}

void Hdf5Loader::SetFramePtr(Frame* frame) {
    _frame = frame;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
