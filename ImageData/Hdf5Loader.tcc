
#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_

#include "Hdf5Loader.h"

namespace carta {

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

template <typename T>
const Hdf5Loader::IPos Hdf5Loader::GetStatsDataShapeTyped(FileInfo::Data ds) {
    casacore::HDF5DataSet data_set(*(_image->Group()), DataSetToString(ds), (const T*)0);
    return data_set.shape();
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

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_
