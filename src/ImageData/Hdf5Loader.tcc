/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_
#define CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_

#include "Hdf5Loader.h"

#include "Util/Image.h"

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
    auto image = GetImage();
    if (!image) {
        return IPos();
    }

    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(image.get());
    casacore::HDF5DataSet data_set(*(hdf5_image->Group()), DataSetToString(ds), (const T*)0);
    return data_set.shape();
}

// TODO: We need to use the C API to read scalar datasets for now, but we should patch casacore to handle them correctly.
template <typename S, typename D>
casacore::ArrayBase* Hdf5Loader::GetStatsDataTyped(FileInfo::Data ds) {
    casacore::ArrayBase* data = new casacore::Array<D>();

    auto image = GetImage();
    if (!image) {
        return data;
    }

    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(image.get());
    casacore::HDF5DataSet data_set(*(hdf5_image->Group()), DataSetToString(ds), (const S*)0);

    if (data_set.shape().size() == 0) {
        // Scalar dataset hackaround
        D value;
        casacore::HDF5DataType data_type((D*)0);
        H5Dread(data_set.getHid(), data_type.getHidMem(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &value);
        casacore::ArrayBase* scalar = new casacore::Array<D>(IPos(1, 1), value);
        return scalar;
    }

    data_set.get(casacore::Slicer(IPos(data_set.shape().size(), 0), data_set.shape()), *data);
    return data;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_HDF5LOADER_TCC_
