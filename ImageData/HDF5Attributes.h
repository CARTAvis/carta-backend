//# HDF5Attributes.h: get HDF5 header attributes in casacore::Record

#pragma once

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

class HDF5Attributes {

public:
    // HDF5Record::doReadRecord modified to not iterate through links
    // (links get HDF5Error "Could not open group XXX in parent")
    static casacore::Record DoReadAttributes(hid_t groupHid);

    // These attributes may be string type instead of numerical type
    static bool GetIntAttribute(casacore::Int64& val, const casacore::Record& rec,
        const casacore::String& field);
    static bool GetDoubleAttribute(casacore::Double& val, const casacore::Record& rec,
        const casacore::String& field);
    static void ConvertToFits(casacore::Record& in);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static void ReadScalar (hid_t attrId, hid_t dtid, const casacore::String& name,
        casacore::RecordInterface& rec);
};

