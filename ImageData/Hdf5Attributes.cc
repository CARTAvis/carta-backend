//# Hdf5Attributes.cc: get HDF5 header attributes in casacore::Record
#include "Hdf5Attributes.h"

#include <cstring>

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>

casacore::Record Hdf5Attributes::ReadAttributes(hid_t group_hid) {
    // reads attributes but not links
    casacore::Record rec;
    char cname[512];
    int num_fields = H5Aget_num_attrs(group_hid);
    // Iterate through the attributes in order of index, so we're sure
    // they are read back in the same order as written.
    for (int index = 0; index < num_fields; ++index) {
        casacore::HDF5HidAttribute id(H5Aopen_idx(group_hid, index));
        AlwaysAssert(id >= 0, casacore::AipsError);
        AlwaysAssert(id.getHid() >= 0, casacore::AipsError);
        unsigned int name_size = H5Aget_name(id, sizeof(cname), cname);
        AlwaysAssert(name_size < sizeof(cname), casacore::AipsError);
        casacore::String name(cname);
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
            ReadScalar(id, dtid, name, rec);
        }
        H5Aclose(id);
    }
    return rec;
}

void Hdf5Attributes::ReadScalar(hid_t attr_id, hid_t data_type_id, const casacore::String& name, casacore::RecordInterface& rec) {
    // Handle a scalar field.
    int sz = H5Tget_size(data_type_id);
    switch (H5Tget_class(data_type_id)) {
        case H5T_INTEGER: {
            casacore::Int64 value;
            casacore::HDF5DataType data_type((casacore::Int64*)0);
            H5Aread(attr_id, data_type.getHidMem(), &value);
            rec.define(name, value);
        } break;
        case H5T_FLOAT: {
            casacore::Double value;
            casacore::HDF5DataType data_type((casacore::Double*)0);
            H5Aread(attr_id, data_type.getHidMem(), &value);
            rec.define(name, value);
        } break;
        case H5T_STRING: {
            casacore::String value;
            value.resize(sz + 1);
            casacore::HDF5DataType data_type(value);
            H5Aread(attr_id, data_type.getHidMem(), const_cast<char*>(value.c_str()));
            value.resize(std::strlen(value.c_str()));
            rec.define(name, value);
        } break;
        default:
            throw casacore::HDF5Error("Unknown data type of scalar attribute " + name);
    }
}

// get int value (might be string)
bool Hdf5Attributes::GetIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field) {
    bool get_ok(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asInt64(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toInt(rec.asString(field));
            } catch (casacore::AipsError& err) {
                get_ok = false;
            }
        }
    } else {
        get_ok = false;
    }
    return get_ok;
}

// get double value (might be string)
bool Hdf5Attributes::GetDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field) {
    bool get_ok(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asDouble(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toDouble(rec.asString(field));
            } catch (casacore::AipsError& err) {
                get_ok = false;
            }
        }
    } else {
        get_ok = false;
    }
    return get_ok;
}

