/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_TCC_
#define CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_TCC_

#include "CartaFitsImage.h"

#include <casacore/casa/Arrays/ArrayMath.h>

namespace carta {

template <typename T>
bool CartaFitsImage::GetDataSubset(fitsfile* fptr, int datatype, const casacore::Slicer& section, casacore::Array<float>& buffer) {
    // Read section of data from FITS file and put it in buffer

    // Get section components (convert to 1-based)
    std::vector<long> start, end, inc;
    casacore::IPosition slicer_start = section.start();
    casacore::IPosition slicer_end = section.end();
    casacore::IPosition slicer_stride = section.stride();
    for (int i = 0; i < slicer_start.size(); ++i) {
        start.push_back(slicer_start[i] + 1);
        end.push_back(slicer_end[i] + 1);
        inc.push_back(slicer_stride[i]);
    }

    // Read data into temporary buffer
    casacore::IPosition buffer_shape = section.length();
    auto buffer_size = buffer_shape.product();
    std::vector<T> tmp_buffer(buffer_size);
    casacore::Array<T> tmp_array(buffer_shape, tmp_buffer.data(), casacore::StorageInitPolicy::SHARE);

    // cfitsio params
    int anynul(0), status(0);
    T null_val(0);
    int dtype(TFLOAT);

    switch (datatype) {
        case 8:
            dtype = TBYTE;
            break;
        case 16:
            dtype = TSHORT;
            break;
        case 32:
            dtype = TINT;
            break;
        case 64:
            dtype = TLONGLONG;
            break;
        case -32:
            dtype = TFLOAT;
            break;
        case -64:
            dtype = TDOUBLE;
            break;
    }

    fits_read_subset(fptr, dtype, start.data(), end.data(), inc.data(), &null_val, tmp_buffer.data(), &anynul, &status);
    if (status > 0) {
        spdlog::debug("fits_read_subset exited with status {}", status);
        return false;
    }

    // Convert <T> to <float>
    buffer.resize(tmp_array.shape());
    convertArray(buffer, tmp_array);

    return true;
}

template <typename T>
bool CartaFitsImage::GetPixelMask(fitsfile* fptr, int datatype, const casacore::IPosition& shape, casacore::ArrayLattice<bool>& mask) {
    // Return mask for entire image
    auto mask_size = shape.product();
    std::vector<char> mask_buffer(mask_size);
    casacore::Array<char> marray(shape, mask_buffer.data(), casacore::StorageInitPolicy::SHARE);
    std::vector<T> data_buffer(mask_size);

    // cfitsio params
    std::vector<long> start(shape.size(), 1);
    int anynul(0), status(0);
    int dtype(TFLOAT);

    switch (datatype) {
        case 8:
            dtype = TBYTE;
            break;
        case 16:
            dtype = TSHORT;
            break;
        case 32:
            dtype = TINT;
            break;
        case 64:
            dtype = TLONGLONG;
            break;
        case -32:
            dtype = TFLOAT;
            break;
        case -64:
            dtype = TDOUBLE;
            break;
    }

    fits_read_pixnull(fptr, dtype, start.data(), mask_size, data_buffer.data(), mask_buffer.data(), &anynul, &status);
    if (status > 0) {
        spdlog::debug("fits_read_pixnull exited with status {}", status);
        return false;
    }

    // Convert <char> to <bool>
    casacore::Array<bool> mask_array(marray.shape());
    convertArray(mask_array, marray);
    mask = casacore::ArrayLattice<bool>(mask_array);
    return true;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_TCC_
