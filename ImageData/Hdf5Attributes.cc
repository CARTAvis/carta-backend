//# Hdf5Attributes.cc: get HDF5 header attributes in casacore::Record
#include "Hdf5Attributes.h"

#include <cstring>

#include <casacore/casa/HDF5/HDF5DataType.h>
#include <casacore/casa/HDF5/HDF5Error.h>
#include <casacore/fits/FITS/FITSDateUtil.h>

casacore::Record Hdf5Attributes::ReadAttributes(hid_t group_hid) {
    // reads attributes but not links
    casacore::Record attributes_record;
    char cname[512];
    int num_fields = H5Aget_num_attrs(group_hid);
    // Iterate through the attributes in order of index, so we're sure they are read back in the same order as written.
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
            ReadScalar(id, dtid, name, attributes_record);
        }
        H5Aclose(id);
    }
    ConvertAttributeStrings(attributes_record);
    return attributes_record;
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

void Hdf5Attributes::ConvertAttributeStrings(casacore::Record& attributes) {
    // convert specified keywords to bool, int, or double
    std::vector<std::string> skip_entries{"SCHEMA_VERSION", "HDF5_CONVERTER", "HDF5_CONVERTER_VERSION"};
    std::vector<std::string> int_entries{"BITPIX", "WCSAXES", "A_ORDER", "B_ORDER"};
    std::vector<std::string> double_entries{"EQUINOX", "EPOCH", "LONPOLE", "LATPOLE", "RESTFRQ", "OBSFREQ", "MJD-OBS", "DATAMIN", "DATAMAX",
                                           "BMAJ", "BMIN", "BPA"};
    std::vector<std::string> substr_entries{"CRVAL", "CRPIX", "CDELT", "CROTA"};
    std::vector<std::string> prefix_entries{"A_", "B_", "CD"};

    casacore::Record header_record;
    for (int i = 0; i < attributes.nfields(); ++i) {
        const std::string entry_name = attributes.name(i);
        const std::string entry_name_substr = entry_name.substr(0,5);
        const std::string entry_name_prefix = entry_name.substr(0,2);

        if (std::find(skip_entries.begin(), skip_entries.end(), entry_name) != skip_entries.end()) {
            continue;
        }

        switch (attributes.type(i)) {
            case casacore::DataType::TpString: {
                casacore::String entry_value = attributes.asString(i);
                if ((entry_name == "SIMPLE") || (entry_name == "EXTEND") || (entry_name == "BLOCKED")) { // bool
                    header_record.define(entry_name, (entry_value == "T" ? 1 : 0));
                } else if ((std::find(int_entries.begin(), int_entries.end(), entry_name) != int_entries.end()) ||
                           (entry_name_substr == "NAXIS")) {
                    header_record.define(entry_name, std::stoi(entry_value)); // int
                } else if (std::find(double_entries.begin(), double_entries.end(), entry_name) != double_entries.end()) {
                    header_record.define(entry_name, std::stod(entry_value)); // double
                } else if (std::find(substr_entries.begin(), substr_entries.end(), entry_name_substr) != substr_entries.end()) {
                    header_record.define(entry_name, std::stod(entry_value)); // double
                } else if (std::find(prefix_entries.begin(), prefix_entries.end(), entry_name_prefix) != prefix_entries.end()) {
                    header_record.define(entry_name, std::stod(entry_value)); // double
                } else {
                    entry_value = (entry_value == "Kelvin" ? "K" : entry_value);
                    if (entry_name == "DATE-OBS") { // date
                        casacore::String fits_date;
                        casacore::FITSDateUtil::convertDateString(fits_date, entry_value);
                        header_record.define(entry_name, fits_date);
                    } else {
                        header_record.define(entry_name, entry_value); // string
                    }
                }
            } break;
            case casacore::DataType::TpInt64: {
                header_record.define(entry_name, attributes.asInt64(i));
            } break;
            case casacore::DataType::TpDouble: {
                header_record.define(entry_name, attributes.asDouble(i));
            } break;
            default:
                break;
        }
    }
    attributes = header_record;
}
