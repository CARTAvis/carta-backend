/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_TCC_
#define CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_TCC_

#include "CartaFitsImage.h"

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>

namespace carta {

template <typename T>
bool CartaFitsImage::GetDataSubset(int datatype, const casacore::Slicer& section, casacore::Array<float>& buffer) {
    // Read section of data from FITS file and put it in buffer
    // Get section components for cfitsio(convert to 1-based)
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
    int anynul(0);
    T* null_val(nullptr);
    int status(0);

    std::unique_lock<std::mutex> ulock(_fptr_mutex);
    auto fptr = OpenFile();
    if (!fptr) {
        spdlog::debug("CartaFitsImage failed to get file ptr to read subset.");
        return false;
    }

    switch (datatype) {
        case 8: {
            fits_read_subset(fptr, TBYTE, start.data(), end.data(), inc.data(), &null_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
        case 16: {
            fits_read_subset(fptr, TSHORT, start.data(), end.data(), inc.data(), &null_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
        case 32: {
            fits_read_subset(fptr, TINT, start.data(), end.data(), inc.data(), &null_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
        case 64: {
            fits_read_subset(fptr, TLONGLONG, start.data(), end.data(), inc.data(), &null_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
        case -32: {
            float* fnull_val(nullptr);
            fits_read_subset(fptr, TFLOAT, start.data(), end.data(), inc.data(), fnull_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
        case -64: {
            double dnull_val(NAN);
            fits_read_subset(fptr, TDOUBLE, start.data(), end.data(), inc.data(), &dnull_val, tmp_buffer.data(), &anynul, &status);
            break;
        }
    }
    ulock.unlock();

    if (status > 0) {
        fits_report_error(stderr, status);
        spdlog::debug("fits_read_subset exited with status {}", status);
        return false;
    }

    // Convert <T> to <float>
    buffer.resize(tmp_array.shape());
    convertArray(buffer, tmp_array);

    return true;
}

template <typename T>
bool CartaFitsImage::GetPixelMask(int datatype, const casacore::IPosition& shape, casacore::ArrayLattice<bool>& mask) {
    // Return mask for entire image
    auto mask_size = shape.product();
    std::vector<char> mask_buffer(mask_size);
    casacore::Array<char> marray(shape, mask_buffer.data(), casacore::StorageInitPolicy::SHARE);
    std::vector<T> data_buffer(mask_size);

    // cfitsio params: undefined pixels are true (1)
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

    std::unique_lock<std::mutex> ulock(_fptr_mutex);
    auto fptr = OpenFile();
    if (!fptr) {
        spdlog::debug("CartaFitsImage failed to get file ptr to read mask.");
        return false;
    }

    fits_read_pixnull(fptr, dtype, start.data(), mask_size, data_buffer.data(), mask_buffer.data(), &anynul, &status);
    ulock.unlock();

    if (status > 0) {
        spdlog::debug("fits_read_pixnull exited with status {}", status);
        return false;
    }

    // Convert <char> to <bool>; invert bool so masked (good) pixels are 1
    casacore::Array<bool> mask_array(marray.shape());
    convertArray(mask_array, marray);
    mask = casacore::ArrayLattice<bool>(!mask_array);
    return true;
}

template <typename T>
bool CartaFitsImage::GetNanPixelMask(casacore::ArrayLattice<bool>& mask) {
    // Pixel mask for entire image
    auto mask_array = mask.asArray();
    mask_array.resize(shape());

    casacore::SubImage<T> sub_image(dynamic_cast<casacore::ImageInterface<T>&>(*this));
    casacore::RO_MaskedLatticeIterator<T> lattice_iter(sub_image);

    for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
        casacore::Array<T> cursor_data = lattice_iter.cursor();
        casacore::Array<bool> cursor_mask = isFinite(cursor_data);

        casacore::Slicer cursor_slicer(lattice_iter.position(), lattice_iter.cursorShape());
        mask_array(cursor_slicer) = cursor_mask;
    }

    return true;
}

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_TCC_
