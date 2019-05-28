#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_

#include <unordered_map>

#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Hdf5Attributes.h"

namespace carta {

class Hdf5Loader : public FileLoader {
public:
    Hdf5Loader(const std::string& filename);
    void OpenFile(const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int cursor_y) override;
    bool GetRegionSpectralData(
        std::map<CARTA::StatsType, std::vector<double>>& data, int stokes, const casacore::ArrayLattice<casacore::Bool>* mask, casacore::IPosition origin, const std::vector<int>& stats_types) override;

protected:
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) override;

private:
    std::string _filename;
    std::string _hdu;
    std::unique_ptr<CartaHdf5Image> _image;
    std::unique_ptr<casacore::HDF5Lattice<float>> _swizzled_image;

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

void Hdf5Loader::OpenFile(const std::string& hdu) {
    // Open hdf5 image with specified hdu
    _image = std::unique_ptr<CartaHdf5Image>(new CartaHdf5Image(_filename, DataSetToString(FileInfo::Data::Image), hdu));

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

bool Hdf5Loader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int cursor_y) {
    bool data_ok(false);
    if (HasData(FileInfo::Data::SWIZZLED)) {
        casacore::Slicer slicer;
        if (_num_dims == 4) {
            slicer = casacore::Slicer(IPos(4, 0, cursor_y, cursor_x, stokes), IPos(4, _num_channels, 1, 1, 1));
        } else if (_num_dims == 3) {
            slicer = casacore::Slicer(IPos(3, 0, cursor_y, cursor_x), IPos(3, _num_channels, 1, 1));
        }

        data.resize(_num_channels);
        casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        try {
            LoadSwizzledData(FileInfo::Data::SWIZZLED)->doGetSlice(tmp, slicer);
            data_ok = true;
        } catch (casacore::AipsError& err) {
            std::cerr << "AIPS ERROR: " << err.getMesg() << std::endl;
        }
    }

    return data_ok;
}

bool Hdf5Loader::GetRegionSpectralData(
    std::map<CARTA::StatsType, std::vector<double>>& data, int stokes, const casacore::ArrayLattice<casacore::Bool>* mask, casacore::IPosition origin, const std::vector<int>& stats_types) {
    
    bool data_ok(false);
    
    int num_z = _num_channels;
    int num_y = mask->shape()(0);
    int num_x = mask->shape()(1);
    
    int x_min = origin(0);
    int x_max = x_min + num_x;
    int y_min = origin(1);
    int y_max = y_min + num_y;
    
    std::vector<double> sum(num_z);
    std::vector<double> min(num_z);
    std::vector<double> max(num_z);
    std::vector<double> std_dev(num_z);
    std::vector<int64_t> nan_count(num_z);
    std::vector<int64_t> valid_count(num_z);
    
    

    return data_ok;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_H_
