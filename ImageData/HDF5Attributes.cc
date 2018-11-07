//# HDF5Attributes.cc: get HDF5 header attributes in casacore::Record

#include "HDF5Attributes.h"

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>

casacore::Record HDF5Attributes::doReadAttributes (hid_t groupHid) {
    // reads attributes but not links
    casacore::Record rec;
    char cname[512];
    int nfields = H5Aget_num_attrs (groupHid);
    // Iterate through the attributes in order of index, so we're sure
    // they are read back in the same order as written.
    for (int index=0; index<nfields; ++index) {
        casacore::HDF5HidAttribute id(H5Aopen_idx(groupHid, index));
        AlwaysAssert (id.getHid()>=0, casacore::AipsError);
        unsigned int namsz = H5Aget_name(id, sizeof(cname), cname);
        AlwaysAssert (namsz<sizeof(cname), casacore::AipsError);
        casacore::String name(cname);
        // Get rank and shape from the dataspace info.
        casacore::HDF5HidDataSpace dsid (H5Aget_space(id));
        int rank = H5Sget_simple_extent_ndims(dsid);
        if (rank > 0) {
          casacore::Block<hsize_t> shp(rank);
          rank = H5Sget_simple_extent_dims(dsid, shp.storage(), NULL);
        }
        // Get data type and its size.
        if (rank == 0) {
          casacore::HDF5HidDataType dtid(H5Aget_type(id));
          readScalar (id, dtid, name, rec);
        }
    }
    return rec;
}

void HDF5Attributes::readScalar (hid_t attrId, hid_t dtid, const casacore::String& name,
    casacore::RecordInterface& rec) {
    // Handle a scalar field.
    int sz = H5Tget_size(dtid);
    switch (H5Tget_class(dtid)) {
        case H5T_INTEGER: {
            casacore::Int64 value;
            casacore::HDF5DataType dtype((casacore::Int64*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
            }
            break;
        case H5T_FLOAT: {
            casacore::Double value;
            casacore::HDF5DataType dtype((casacore::Double*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
            }
            break;
        case H5T_STRING: {
            casacore::String value;
            value.resize(sz+1);
            casacore::HDF5DataType dtype(value);
            H5Aread(attrId, dtype.getHidMem(), const_cast<char*>(value.c_str()));
            value.resize(sz);
            rec.define(name, value);
            }
            break;
        default: 
           throw casacore::HDF5Error ("Unknown data type of scalar attribute " + name);
    }
}

