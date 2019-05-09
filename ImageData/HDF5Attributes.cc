//# HDF5Attributes.cc: get HDF5 header attributes in casacore::Record
#include "HDF5Attributes.h"

#include <cstring>

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>
#include <casacore/fits/FITS/FITSDateUtil.h>

casacore::Record HDF5Attributes::DoReadAttributes(hid_t groupHid) {
    // reads attributes but not links
    casacore::Record rec;
    char cname[512];
    int nfields = H5Aget_num_attrs(groupHid);
    // Iterate through the attributes in order of index, so we're sure
    // they are read back in the same order as written.
    for (int index = 0; index < nfields; ++index) {
        casacore::HDF5HidAttribute id(H5Aopen_idx(groupHid, index));
        AlwaysAssert(id.getHid() >= 0, casacore::AipsError);
        unsigned int namsz = H5Aget_name(id, sizeof(cname), cname);
        AlwaysAssert(namsz < sizeof(cname), casacore::AipsError);
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
    }
    return rec;
}

void HDF5Attributes::ReadScalar(hid_t attrId, hid_t dtid, const casacore::String& name, casacore::RecordInterface& rec) {
    // Handle a scalar field.
    int sz = H5Tget_size(dtid);
    switch (H5Tget_class(dtid)) {
        case H5T_INTEGER: {
            casacore::Int64 value;
            casacore::HDF5DataType dtype((casacore::Int64*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
        } break;
        case H5T_FLOAT: {
            casacore::Double value;
            casacore::HDF5DataType dtype((casacore::Double*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
        } break;
        case H5T_STRING: {
            casacore::String value;
            value.resize(sz + 1);
            casacore::HDF5DataType dtype(value);
            H5Aread(attrId, dtype.getHidMem(), const_cast<char*>(value.c_str()));
            value.resize(std::strlen(value.c_str()));
            rec.define(name, value);
        } break;
        default:
            throw casacore::HDF5Error("Unknown data type of scalar attribute " + name);
    }
}

// get int value (might be string)
bool HDF5Attributes::GetIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field) {
    bool getOK(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asInt64(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toInt(rec.asString(field));
            } catch (casacore::AipsError& err) {
                getOK = false;
            }
        }
    } else {
        getOK = false;
    }
    return getOK;
}

// get double value (might be string)
bool HDF5Attributes::GetDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field) {
    bool getOK(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asDouble(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toDouble(rec.asString(field));
            } catch (casacore::AipsError& err) {
                getOK = false;
            }
        }
    } else {
        getOK = false;
    }
    return getOK;
}

void HDF5Attributes::ConvertToFits(casacore::Record& in) {
    // hdf5 attribute data types are non-standardized
    casacore::Record out;
    if (in.isDefined("BITPIX")) { // should be second keyword
        int val;
        casacore::DataType type(in.type(in.fieldNumber("BITPIX")));
        if (type == casacore::TpString)
            val = casacore::String::toInt(in.asString("BITPIX"));
        else
            val = in.asInt("BITPIX");
        out.define("BIXPIX", val);
    }
    for (unsigned int i = 0; i < in.nfields(); ++i) {
        casacore::String name(in.name(i));
        if (name.size() > 8 || (name == "SIMPLE")) {
            // HDF5 keywords not needed, SIMPLE will be added
            continue;
        }
        casacore::DataType type(in.type(i));
        switch (type) {
            case casacore::TpString: {
                casacore::String str_val(in.asString(name));
                if (str_val == "Kelvin")
                    str_val = "K";
                if (name == "BITPIX")
                    break;
                if ((name == "SIMPLE") || (name == "EXTEND") || (name == "BLOCKED")) {
                    // convert to bool
                    bool val = (str_val == "T" ? true : false);
                    out.define(name, val);
                } else if (name.startsWith("NAXIS")) {
                    // convert to int
                    int val = casacore::String::toInt(str_val);
                    out.define(name, val);
                } else if ((name == "EQUINOX") || (name == "EPOCH") || (name == "LONPOLE") || (name == "LATPOLE") || (name == "RESTFRQ") ||
                           (name == "MJD-OBS") || (name == "DATAMIN") || (name == "DATAMAX") || (name.startsWith("CRVAL")) ||
                           (name.startsWith("CRPIX")) || (name.startsWith("CDELT")) || (name.startsWith("CROTA"))) {
                    // convert to float
                    float val = casacore::String::toFloat(str_val);
                    out.define(name, val);
                } else if (name == "DATE-OBS") {
                    // convert format
                    casacore::String date_out;
                    casacore::FITSDateUtil::convertDateString(date_out, str_val);
                    out.define(name, date_out);
                } else {
                    str_val.trim();
                    out.define(name, str_val);
                }
                break;
            }
            case casacore::TpInt: {
                out.define(name, in.asInt(name));
                break;
            }
            case casacore::TpDouble: {
                out.define(name, in.asFloat(name));
                break;
            }
            default:
                break;
        }
    }
    in.assign(out);
}
