/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Hdf5Attributes.cc: get HDF5 header attributes in casacore::Record
#include "Hdf5Attributes.h"

#include <cstring>

#include <fmt/format.h>

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>

casacore::Vector<casacore::String> Hdf5Attributes::ReadAttributes(hid_t group_hid) {
    // Reads attributes into FITS-format "name = value" strings
    char cname[512];
    // Iterate through the attributes in order of index, so we're sure they are read back in the same order as written.
    int num_fields = H5Aget_num_attrs(group_hid);
    casacore::Vector<casacore::String> headers(num_fields);
    int iheader(0);
    for (int index = 0; index < num_fields; ++index) {
        casacore::HDF5HidAttribute id(H5Aopen_idx(group_hid, index));
        AlwaysAssert(id >= 0, casacore::AipsError);
        AlwaysAssert(id.getHid() >= 0, casacore::AipsError);
        unsigned int name_size = H5Aget_name(id, sizeof(cname), cname);
        AlwaysAssert(name_size < sizeof(cname), casacore::AipsError);
        std::string name(cname);
        // Get rank and shape from the dataspace info.
        casacore::HDF5HidDataSpace dsid(H5Aget_space(id));
        int rank = H5Sget_simple_extent_ndims(dsid);
        if (rank > 0) {
            casacore::Block<hsize_t> shp(rank);
            rank = H5Sget_simple_extent_dims(dsid, shp.storage(), NULL);
        }
        // Get data type and its size.
        if (rank == 0) {
            casacore::HDF5HidDataType dtid(H5Aget_type(id));
            std::string value = ReadScalar(id, dtid, name);
            headers(iheader++) = value;
        }
        H5Aclose(id);
    }
    headers.resize(iheader + 1, true);
    headers(iheader) = "END";
    return headers;
}

std::string Hdf5Attributes::ReadScalar(hid_t attr_id, hid_t data_type_id, const std::string& name) {
    // Handle a scalar field.
    int sz = H5Tget_size(data_type_id);
    switch (H5Tget_class(data_type_id)) {
        case H5T_INTEGER: {
            if ((sz == 4) && (H5Tget_sign(data_type_id) == H5T_SGN_2)) {
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
            std::ostringstream ostream;
            ostream.precision(13);
            ostream << value;
            std::string key_value = fmt::format("{:<8}= {}", name, ostream.str());
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
