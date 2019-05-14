//# Hdf5Attributes.h: get HDF5 header attributes in casacore::Record
#ifndef CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_

#pragma once

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

class Hdf5Attributes {
public:
    // HDF5Record::doReadRecord modified to not iterate through links
    // (links get HDF5Error "Could not open group XXX in parent")
    static casacore::Record ReadAttributes(hid_t group_hid);

    // These attributes may be string type instead of numerical type
    static bool GetIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field);
    static bool GetDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static void ReadScalar(hid_t attr_id, hid_t data_type_id, const casacore::String& name, casacore::RecordInterface& rec);
};

#endif // CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
