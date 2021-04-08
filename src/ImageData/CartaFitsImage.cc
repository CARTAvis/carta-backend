/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaFitsImage.cc : specialized casacore::ImageInterface implementation for FITS

#include "CartaFitsImage.h"

#include <casacore/casa/OS/File.h>

#include "../Logger/Logger.h"

using namespace carta;

CartaFitsImage::CartaFitsImage(const std::string& filename, unsigned int hdu)
    : casacore::ImageInterface<float>(),
      _filename(filename),
      _hdu(hdu),
      _is_compressed(false),
      _datatype(casacore::TpOther),
      _has_blanks(false),
      _uchar_blank(0),
      _short_blank(0),
      _int_blank(0),
      _longlong_blank(0),
      _pixel_mask(nullptr) {
    casacore::File ccfile(filename);
    if (!ccfile.exists() || !ccfile.isReadable()) {
        throw(casacore::AipsError("FITS file is not readable or does not exist."));
    }

    SetUpImage();
}

CartaFitsImage::CartaFitsImage(const CartaFitsImage& other)
    : ImageInterface<float>(other),
      _filename(other._filename),
      _hdu(other._hdu),
      _shape(other._shape),
      _is_compressed(other._is_compressed),
      _datatype(other._datatype),
      _has_blanks(other._has_blanks),
      _uchar_blank(other._uchar_blank),
      _short_blank(other._short_blank),
      _int_blank(other._int_blank),
      _longlong_blank(other._longlong_blank),
      _pixel_mask(nullptr) {
    if (other._pixel_mask != nullptr) {
        _pixel_mask = other._pixel_mask->clone();
    }
}

CartaFitsImage::~CartaFitsImage() {
    delete _pixel_mask;
}

// Image interface

casacore::String CartaFitsImage::imageType() const {
    return "CartaFitsImage";
}

casacore::String CartaFitsImage::name(bool stripPath) const {
    if (stripPath) {
        casacore::Path path(_filename);
        return path.baseName();
    } else {
        return _filename;
    }
}

casacore::IPosition CartaFitsImage::shape() const {
    return _shape;
}

casacore::Bool CartaFitsImage::ok() const {
    return true;
}

casacore::DataType CartaFitsImage::dataType() const {
    switch (_datatype) {
        case 8:
            return casacore::DataType::TpUChar;
        case 16:
            return casacore::DataType::TpShort;
        case 32:
            return casacore::DataType::TpInt;
        case 64:
            return casacore::DataType::TpInt64;
        case -32:
            return casacore::DataType::TpFloat;
        case -64:
            return casacore::DataType::TpDouble;
    }

    return casacore::DataType::TpOther;
}

casacore::Bool CartaFitsImage::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    /* TODO
    reopenIfNeeded();
    if (pTiledFile_p->dataType() == TpFloat) {
       pTiledFile_p->get (buffer, section);
    } else if (pTiledFile_p->dataType() == TpDouble) {
       Array<Double> tmp;
       pTiledFile_p->get (tmp, section);
       buffer.resize(tmp.shape());
       convertArray(buffer, tmp);
    } else if (pTiledFile_p->dataType() == TpInt) {
       pTiledFile_p->get (buffer, section, scale_p, offset_p, longMagic_p, hasBlanks_p);
    } else if (pTiledFile_p->dataType() == TpShort) {
       pTiledFile_p->get (buffer, section, scale_p, offset_p, shortMagic_p, hasBlanks_p);
    } else if (pTiledFile_p->dataType() == TpUChar) {
       pTiledFile_p->get (buffer, section, scale_p, offset_p, uCharMagic_p, hasBlanks_p);
    }
    */

    return false;
}

void CartaFitsImage::doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
    throw(casacore::AipsError("CartaFitsImage::doPutSlice - image is not writable"));
}

const casacore::LatticeRegion* CartaFitsImage::getRegionPtr() const {
    return nullptr;
}

casacore::ImageInterface<float>* CartaFitsImage::cloneII() const {
    return new CartaFitsImage(*this);
}

void CartaFitsImage::resize(const casacore::TiledShape& newShape) {
    throw(casacore::AipsError("CartaFitsImage::resize - image is not writable"));
}

casacore::uInt CartaFitsImage::advisedMaxPixels() const {
    // TODO
    // return shape_p.tileShape().product();
    return 0;
}

casacore::IPosition CartaFitsImage::doNiceCursorShape(casacore::uInt maxPixels) const {
    // TODO
    // return shape_p.tileShape();
    return casacore::IPosition();
}

casacore::Bool CartaFitsImage::isMasked() const {
    return _has_blanks;
}

casacore::Bool CartaFitsImage::hasPixelMask() const {
    return _has_blanks;
}

const casacore::Lattice<bool>& CartaFitsImage::pixelMask() const {
    if (!_has_blanks) {
        throw(casacore::AipsError("CartaFitsImage::pixelMask - no pixel mask used"));
    }

    return *_pixel_mask;
}

casacore::Lattice<bool>& CartaFitsImage::pixelMask() {
    if (!_has_blanks) {
        throw(casacore::AipsError("CartaFitsImage::pixelMask - no pixel mask used"));
    }

    return *_pixel_mask;
}

casacore::Bool CartaFitsImage::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    if (!_has_blanks) {
        buffer.resize(section.length());
        buffer = true;
        return false;
    }

    return _pixel_mask->getSlice(buffer, section);
}

// private

void CartaFitsImage::CloseFile(fitsfile* fptr) {
    int status = 0;
    fits_close_file(fptr, &status);
}

void CartaFitsImage::CloseFileIfError(fitsfile* fptr, const int& status, const std::string& error) {
    // Close if status is not 0.  Optionally throw exception described by error.
    if (status) {
        CloseFile(fptr);

        if (!error.empty()) {
            throw(casacore::AipsError(error));
        }
    }
}

void CartaFitsImage::SetUpImage() {
    casacore::Vector<casacore::String> headers = GetFitsHeaders();
    throw(casacore::AipsError("SetUpImage incomplete."));
}

casacore::Vector<casacore::String> CartaFitsImage::GetFitsHeaders() {
    // Read header values into string vector, and store some values.
    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);
    if (status) {
        throw(casacore::AipsError("Error opening FITS file."));
    }

    // Advance to requested hdu
    int hdu(_hdu + 1);
    int* hdutype(nullptr);
    status = 0;
    fits_movabs_hdu(fptr, hdu, hdutype, &status);
    CloseFileIfError(fptr, status, "Error advancing FITS gz file to requested HDU.");

    // Check hdutype
    if (_hdu > 0) {
        int hdutype(-1);
        status = 0;
        fits_get_hdu_type(fptr, &hdutype, &status); // IMAGE_HDU, ASCII_TBL, BINARY_TBL
        CloseFileIfError(fptr, status, "Error determining HDU type.");

        if (hdutype != IMAGE_HDU) {
            CloseFileIfError(fptr, 1, "No image at specified hdu in FITS file.");
        }
    }

    // Get image parameters: BITPIX, NAXIS, NAXISn
    int maxdim(4), bitpix(0), naxis(0);
    long naxes[maxdim];
    status = 0;
    fits_get_img_param(fptr, maxdim, &bitpix, &naxis, naxes, &status);
    CloseFileIfError(fptr, status, "Error getting image parameters.");
    if (naxis < 2) {
        CloseFileIfError(fptr, 1, "Image must be at least 2D.");
    }

    // Set shape and data type
    _shape.resize(naxis);
    for (int i = 0; i < naxis; ++i) {
        _shape(i) = naxes[i];
    }
    _datatype = bitpix;

    // Set blanks used for integer datatypes (int value for NAN), for pixel mask
    if (bitpix > 0) {
        std::string key("BLANK");
        char* comment(nullptr);
        status = 0;

        switch (_datatype) {
            case 8:
                fits_read_key(fptr, _datatype, key.c_str(), &_uchar_blank, comment, &status);
                break;
            case 16:
                fits_read_key(fptr, _datatype, key.c_str(), &_short_blank, comment, &status);
                break;
            case 32:
                fits_read_key(fptr, _datatype, key.c_str(), &_int_blank, comment, &status);
                break;
            case 64:
                fits_read_key(fptr, _datatype, key.c_str(), &_longlong_blank, comment, &status);
                break;
        }
        _has_blanks = !status;
        spdlog::debug("PDEBUG: has blanks={}", _has_blanks);
    }

    // Get headers to set up image:
    // Previous functions converted compressed headers automatically; must call specific function for uncompressed headers
    // Determine whether tile compressed
    status = 0;
    _is_compressed = fits_is_compressed_image(fptr, &status);
    CloseFileIfError(fptr, status, "Error detecting image compression.");

    // Number of headers (keys)
    int nkeys(0);
    int* more_keys(nullptr);
    status = 0;
    fits_get_hdrspace(fptr, &nkeys, more_keys, &status);
    CloseFileIfError(fptr, status, "Unable to determine FITS headers.");

    // Get headers as single string with no exclusions (exclist=nullptr, nexc=0)
    int no_comments(0);
    char* header[nkeys];
    status = 0;
    if (_is_compressed) {
        // Convert to uncompressed headers
        fits_convert_hdr2str(fptr, no_comments, nullptr, 0, header, &nkeys, &status);
    } else {
        fits_hdr2str(fptr, no_comments, nullptr, 0, header, &nkeys, &status);
    }

    if (status) {
        // Free memory allocated by cfitsio, close file, throw exception
        int free_status(0);
        fits_free_memory(*header, &free_status);
        CloseFileIfError(fptr, status, "Unable to read FITS headers.");
    }

    // Convert headers to Vector of Strings to pass to converter
    std::string hdrstr(header[0]);
    casacore::Vector<casacore::String> headers(nkeys);
    size_t pos(0);
    for (int i = 0; i < nkeys; ++i) {
        headers(i) = hdrstr.substr(pos, 80);
        pos += 80;
    }
    // Free memory allocated by cfitsio
    int free_status(0);
    fits_free_memory(*header, &free_status);

    // Done with file
    CloseFile(fptr);

    return headers;
}
