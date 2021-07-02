/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Hdf5Attributes.cc: get HDF5 header attributes in casacore::Record
#include "Hdf5Attributes.h"

#include <cstring>

#include <spdlog/fmt/fmt.h>

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>

casacore::Vector<casacore::String> Hdf5Attributes::ReadAttributes(hid_t group_hid) {
    // Reads attributes into FITS-format "name = value" strings
    casacore::Vector<casacore::String> headers;
    int num_attrs = H5Aget_num_attrs(group_hid);
    headers.resize(num_attrs + 1);

    auto attr_info = [](hid_t loc_id, const char* name, const H5A_info_t* ainfo, void* opdata) -> herr_t {
        hid_t attr, atype, aspace;
        int rank;
        hsize_t sdim[64];
        herr_t ret;

        auto headers = reinterpret_cast<casacore::Vector<casacore::String>*>(opdata);

        attr = H5Aopen(loc_id, name, H5P_DEFAULT);
        atype = H5Aget_type(attr);
        aspace = H5Aget_space(attr);
        rank = H5Sget_simple_extent_ndims(aspace);
        ret = H5Sget_simple_extent_dims(aspace, sdim, nullptr);

        if (rank == 0 || ret == 0) {
            std::string value = Hdf5Attributes::ReadScalar(attr, atype, name);
            (*headers)(ainfo->corder) = value;
        }

        H5Tclose(atype);
        H5Sclose(aspace);
        H5Aclose(attr);

        return 0;
    };

    // Iterate over the attributes using the order in which they were written
    H5Aiterate2(group_hid, H5_INDEX_CRT_ORDER, H5_ITER_INC, nullptr, attr_info, &headers);
    headers(num_attrs) = "END";

    return headers;
}

std::string Hdf5Attributes::ReadScalar(hid_t attr_id, hid_t data_type_id, const std::string& name) {
    // Handle a scalar field.
    int sz = H5Tget_size(data_type_id);
    switch (H5Tget_class(data_type_id)) {
        case H5T_INTEGER: {
            if ((sz == 1) && (H5Tget_sign(data_type_id) == H5T_SGN_NONE)) {
                casacore::Bool value;
                casacore::HDF5DataType data_type((casacore::Bool*)0);
                H5Aread(attr_id, data_type.getHidMem(), &value);
                std::string value_string = (value ? "T" : "F");
                std::string key_value = fmt::format("{:<8}= {}", name, value_string);
                return fmt::format("{:<80}", key_value);
            } else {
                casacore::Int64 value;
                casacore::HDF5DataType data_type((casacore::Int64*)0);
                H5Aread(attr_id, data_type.getHidMem(), &value);
                std::string key_value = fmt::format("{:<8}= {}", name, std::to_string(value));
                return fmt::format("{:<80}", key_value);
            }
        } break;
        case H5T_FLOAT: {
            casacore::Double value;
            casacore::HDF5DataType data_type((casacore::Double*)0);
            H5Aread(attr_id, data_type.getHidMem(), &value);
            std::string key_value = fmt::format("{:<8}= {:#.13G}", name, value);
            return fmt::format("{:<80}", key_value);
        } break;
        case H5T_STRING: {
            casacore::String value;
            value.resize(sz + 1);
            casacore::HDF5DataType data_type(value);
            H5Aread(attr_id, data_type.getHidMem(), const_cast<char*>(value.c_str()));
            value.resize(std::strlen(value.c_str()));
            std::string value_string(value);
            std::string key_value = fmt::format("{:<8}= '{}'", name, value_string);
            return fmt::format("{:<80}", key_value);
        } break;
        default:
            throw casacore::HDF5Error("Unknown data type of scalar attribute " + name);
    }
}
