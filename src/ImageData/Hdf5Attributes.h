/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Hdf5Attributes.h: get HDF5 header attributes in casacore::Record
#ifndef CARTA_SRC_IMAGEDATA_HDF5ATTRIBUTES_H_
#define CARTA_SRC_IMAGEDATA_HDF5ATTRIBUTES_H_

#pragma once

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

namespace carta {

class Hdf5Attributes {
public:
    // Iterate over the attributes and convert them to FITS header entries
    static void ReadAttributes(hid_t group_hid, casacore::Vector<casacore::String>& headers);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static std::string ReadScalar(hid_t attr_id, hid_t data_type_id, const std::string& name);
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_HDF5ATTRIBUTES_H_
