//# Hdf5Attributes.h: get HDF5 header attributes in casacore::Record
#ifndef CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_

#pragma once

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

class Hdf5Attributes {
public:
    // casacore::HDF5Record::doReadRecord modified to not iterate through links
    static casacore::Record ReadAttributes(hid_t group_hid);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static void ReadScalar(hid_t attr_id, hid_t data_type_id, const casacore::String& name, casacore::RecordInterface& rec);
    // Convert keywords from string to proper type
    static void ConvertAttributeStrings(casacore::Record& attributes);
};

#endif // CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
