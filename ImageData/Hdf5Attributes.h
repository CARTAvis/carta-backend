/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Hdf5Attributes.h: get HDF5 header attributes in casacore::Record
#ifndef CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
#define CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_

#pragma once

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5HidMeta.h>

class Hdf5Attributes {
public:
    // casacore::HDF5Record::doReadRecord modified to not iterate through links
    static casacore::Vector<casacore::String> ReadAttributes(hid_t group_hid);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static std::string ReadScalar(hid_t attr_id, hid_t data_type_id, const std::string& name);
};

#endif // CARTA_BACKEND_IMAGEDATA_HDF5ATTRIBUTES_H_
