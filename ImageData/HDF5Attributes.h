//# HDF5Attributes.h: get HDF5 header attributes in casacore::Record

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

//#################################################################################
// HDF5Attributes class

class HDF5Attributes {

public:
    // HDF5Record::doReadRecord modified to not iterate through links
    // (links get HDF5Error "Could not open group XXX in parent")
    static casacore::Record doReadAttributes(hid_t groupHid);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static void readScalar (hid_t attrId, hid_t dtid, const casacore::String& name,
        casacore::RecordInterface& rec);
};

