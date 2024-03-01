/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaMiriadImage.cc : specialized Image implementation to manage headers and masks

#include "CartaMiriadImage.h"

#include <casacore/casa/OS/Path.h>
#include <casacore/mirlib/maxdimc.h>
#include <casacore/mirlib/miriad.h>

using namespace carta;

CartaMiriadImage::CartaMiriadImage(const std::string& filename, casacore::MaskSpecifier mask_spec)
    : casacore::MIRIADImage(filename, mask_spec),
      _filename(filename),
      _mask_spec(mask_spec),
      _is_open(false),
      _has_mask(false),
      _pixel_mask(nullptr) {
    SetUp();
}

CartaMiriadImage::CartaMiriadImage(const CartaMiriadImage& other)
    : casacore::MIRIADImage(other), _filename(other._filename), _mask_spec(other._mask_spec), _pixel_mask(nullptr) {
    SetUp();
    if (other._pixel_mask != nullptr) {
        _pixel_mask = other._pixel_mask->clone();
    }
}

CartaMiriadImage::~CartaMiriadImage() {
    CloseImage();
    if (_pixel_mask != nullptr) {
        delete _pixel_mask;
    }
}

void CartaMiriadImage::SetUp() {
    // open image, set mask, set native spectral type
    OpenImage();     // using mirlib, for checking headers and reading mask
    SetMask();       // casacore::MIRIADImage ignores masks in image
    SetNativeType(); // casacore::MIRIADImage sets default FREQ
}

void CartaMiriadImage::OpenImage() {
    int naxis(MAXNAX), axes[MAXNAX];
    xyopen_c(&_file_handle, _filename.c_str(), "old", naxis, axes);
    _is_open = true;
}

void CartaMiriadImage::CloseImage() {
    xyclose_c(_file_handle);
    _is_open = false;
}

void CartaMiriadImage::SetMask() {
    casacore::String mask_name;
    if (_mask_spec.useDefault()) {
        _mask_name = "mask";
    } else {
        _mask_name = _mask_spec.name();
    }
    _has_mask = hdprsnt_c(_file_handle, _mask_name.c_str()); // check if mask header exists
}

void CartaMiriadImage::SetNativeType() {
    // Read CTYPE header to set native spectral type correctly
    if (coordinates().hasSpectralAxis()) {
        int spectral_axis(coordinates().spectralAxisNumber());           // 0-indexed
        std::string header("ctype" + std::to_string(spectral_axis + 1)); // 1-indexed
        if (hdprsnt_c(_file_handle, header.c_str())) {                   // check if ctype header exists
            char ctype_value[30];
            rdhda_c(_file_handle, header.c_str(), ctype_value, "none", 30);
            if (strncmp(ctype_value, "none", 4) != 0) { // default if not found
                casacore::String spectral_ctype(ctype_value);
                _native_type = casacore::SpectralCoordinate::FREQ;
                if (spectral_ctype.contains("VRAD")) {
                    _native_type = casacore::SpectralCoordinate::VRAD;
                } else if (spectral_ctype.contains("VOPT") || spectral_ctype.contains("FELO")) {
                    _native_type = casacore::SpectralCoordinate::VOPT;
                } else if (spectral_ctype.contains("WAVE")) {
                    _native_type = casacore::SpectralCoordinate::WAVE;
                } else if (spectral_ctype.contains("AWAV")) {
                    _native_type = casacore::SpectralCoordinate::AWAV;
                } else if (spectral_ctype.contains("VELO")) {
                    casacore::MDoppler::Types vel_doppler(coordinates().spectralCoordinate().velocityDoppler());
                    if ((vel_doppler == casacore::MDoppler::Z) || (vel_doppler == casacore::MDoppler::OPTICAL)) {
                        _native_type = casacore::SpectralCoordinate::VOPT;
                    } else {
                        _native_type = casacore::SpectralCoordinate::VRAD;
                    }
                }
            }
        }
    }
}

// Image interface

casacore::String CartaMiriadImage::imageType() const {
    return "CartaMiriadImage";
}

casacore::String CartaMiriadImage::name(bool stripPath) const {
    if (stripPath) {
        casacore::Path full_path(_filename);
        return full_path.baseName();
    } else {
        return _filename;
    }
}

casacore::ImageInterface<float>* CartaMiriadImage::cloneII() const {
    return new CartaMiriadImage(*this);
}

casacore::Bool CartaMiriadImage::isMasked() const {
    return _has_mask;
}

casacore::Bool CartaMiriadImage::hasPixelMask() const {
    return _has_mask;
}

const casacore::Lattice<bool>& CartaMiriadImage::pixelMask() const {
    if (!_has_mask) {
        throw(casacore::AipsError("CartaMiriadImage::pixelMask - no pixelmask used"));
    }
    return pixelMask();
}

casacore::Lattice<bool>& CartaMiriadImage::pixelMask() {
    if (!_has_mask) {
        throw(casacore::AipsError("CartaMiriadImage::pixelMask - no pixelmask used"));
    }

    if (_pixel_mask == nullptr) {
        // fill pixel mask with slicer for entire image shape
        casacore::IPosition data_shape(shape());
        casacore::IPosition start(data_shape.size(), 0);
        casacore::IPosition end(data_shape);
        casacore::Slicer slicer(start, end);
        casacore::Array<bool> array_mask;
        doGetMaskSlice(array_mask, slicer);
        // set pixel mask
        _pixel_mask = new casacore::ArrayLattice<bool>(array_mask);
    }
    return *_pixel_mask;
}

casacore::Bool CartaMiriadImage::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    // return section of mask using mirlib
    casacore::IPosition slicer_shape(section.length());
    buffer.resize(slicer_shape);

    if (!_has_mask) { // use entire section
        buffer.set(true);
        return false;
    }

    if (!_is_open) {
        OpenImage();
    }

    // set each image plane and read flags
    int naxis_plane(slicer_shape.size() - 2); // num axes needed to select image plane
    casacore::IPosition start_pos(section.start()), end_pos(section.end()), stride(section.stride());
    switch (naxis_plane) {
        case 0: { // xy 2D mask
            GetPlaneFlags(buffer, section);
            break;
        }
        case 1: { // xyz 3D mask
            for (int z = start_pos(2); z <= end_pos(2); z += stride(2)) {
                // set 1-based image plane for z and get flags
                int plane_axes[] = {z + 1};
                xysetpl_c(_file_handle, naxis_plane, plane_axes);
                GetPlaneFlags(buffer, section, z);
            }
            break;
        }
        case 2: { // xyzw 4D mask
            for (int w = start_pos(3); w <= end_pos(3); w += stride(3)) {
                for (int z = start_pos(2); z <= end_pos(2); z += stride(2)) {
                    // set 1-based image plane for zw and get flags
                    int plane_axes[] = {z + 1, w + 1};
                    xysetpl_c(_file_handle, naxis_plane, plane_axes);
                    GetPlaneFlags(buffer, section, z, w);
                }
            }
            break;
        }
    }
    return false;
}

void CartaMiriadImage::GetPlaneFlags(casacore::Array<bool>& buffer, const casacore::Slicer& section, int z, int w) {
    // Get flag rows in plane from miriad mask.
    // Assumes plane has been set (xysetpl_c) and buffer is sized to slicer shape
    casacore::IPosition start_pos(section.start()), end_pos(section.end()), stride(section.stride()), length(section.length());

    // Get flags for each row; y is row index in mask file
    for (int y = start_pos(1); y <= end_pos(1); y += stride(1)) {
        // read flags (int) into flag_row
        int flag_row[shape()(0)];                 // use size of x-axis for entire row
        xyflgrd_c(_file_handle, y + 1, flag_row); // 1-based row (y) index in mask file

        // set buffer flags in slicer x-range for this row (y) and plane
        for (int x = start_pos(0); x <= end_pos(0); ++x) {
            // slicer positions are for full image size
            casacore::IPosition image_pos(2, x, y);
            if (w >= 0) {
                image_pos.append(casacore::IPosition(2, z, w));
            } else if (z >= 0) {
                image_pos.append(casacore::IPosition(1, z));
            }
            // buffer position is offset from start
            casacore::IPosition buffer_pos = image_pos - start_pos;
            buffer(buffer_pos) = flag_row[x];
        }
    }
}
