/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaHdf5Image.cc : specialized Image implementation for IDIA HDF5 schema

#include "CartaHdf5Image.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/fits/FITS/FITSDateUtil.h>
#include <casacore/fits/FITS/FITSKeywordUtil.h>
#include <casacore/fits/FITS/fits.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include "../Logger/Logger.h"
#include "Util/String.h"

#include "Hdf5Attributes.h"

using namespace carta;

CartaHdf5Image::CartaHdf5Image(
    const std::string& filename, const std::string& array_name, const std::string& hdu, casacore::MaskSpecifier mask_spec)
    : casacore::ImageInterface<float>(casacore::RegionHandlerHDF5(GetHdf5File, this)),
      _pixel_mask(nullptr),
      _mask_spec(mask_spec),
      _lattice(casacore::CountedPtr<casacore::HDF5File>(new casacore::HDF5File(filename)), array_name, hdu) {
    _shape = _lattice.shape();
    _pixel_mask = new casacore::ArrayLattice<bool>();
    SetUpImage();
}

CartaHdf5Image::CartaHdf5Image(const CartaHdf5Image& other)
    : casacore::ImageInterface<float>(other),
      _pixel_mask(nullptr),
      _mask_spec(other._mask_spec),
      _lattice(other._lattice),
      _shape(other._shape) {
    if (other._pixel_mask != nullptr) {
        _pixel_mask = other._pixel_mask->clone();
    }
}

CartaHdf5Image::~CartaHdf5Image() {
    delete _pixel_mask;
}

casacore::LatticeBase* CartaHdf5Image::OpenCartaHdf5Image(const casacore::String& name, const casacore::MaskSpecifier& spec) {
    return new CartaHdf5Image(name, "DATA", "0", spec);
}

void CartaHdf5Image::RegisterOpenFunction() {
    casacore::ImageOpener::registerOpenImageFunction(casacore::ImageOpener::HDF5, &OpenCartaHdf5Image);
}

// Image interface

casacore::String CartaHdf5Image::imageType() const {
    return "CartaHdf5Image";
}

casacore::String CartaHdf5Image::name(bool stripPath) const {
    return _lattice.name(stripPath);
}

casacore::IPosition CartaHdf5Image::shape() const {
    return _shape;
}

casacore::Bool CartaHdf5Image::ok() const {
    return (_lattice.ndim() == coordinates().nPixelAxes());
}

casacore::Bool CartaHdf5Image::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    return _lattice.doGetSlice(buffer, section);
}

void CartaHdf5Image::doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
    _lattice.doPutSlice(buffer, where, stride);
}

const casacore::LatticeRegion* CartaHdf5Image::getRegionPtr() const {
    return nullptr; // full lattice
}

casacore::ImageInterface<float>* CartaHdf5Image::cloneII() const {
    return new CartaHdf5Image(*this);
}

void CartaHdf5Image::resize(const casacore::TiledShape& newShape) {
    throw(casacore::AipsError("CartaHdf5Image::resize - an HDF5 image cannot be resized"));
}

casacore::Bool CartaHdf5Image::isMasked() const {
    return (_pixel_mask != nullptr);
}

casacore::Bool CartaHdf5Image::hasPixelMask() const {
    return (_pixel_mask != nullptr);
}

const casacore::Lattice<bool>& CartaHdf5Image::pixelMask() const {
    return pixelMask();
}

casacore::Lattice<bool>& CartaHdf5Image::pixelMask() {
    if (!hasPixelMask()) {
        _pixel_mask = new casacore::ArrayLattice<bool>();
    }

    if (_pixel_mask->shape().empty()) {
        // get mask for entire image
        casacore::Array<bool> array_mask;
        casacore::IPosition start(_shape.size(), 0);
        casacore::IPosition end(_shape);
        casacore::Slicer slicer(start, end);
        doGetMaskSlice(array_mask, slicer);
        // replace pixel mask
        delete _pixel_mask;
        _pixel_mask = new casacore::ArrayLattice<bool>(array_mask);
    }
    return *_pixel_mask;
}

casacore::Bool CartaHdf5Image::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    // Set buffer to mask for section of image
    // Slice pixel mask if it is set
    if (hasPixelMask() && !_pixel_mask->shape().empty()) {
        return _pixel_mask->getSlice(buffer, section);
    }

    // Set pixel mask for section only
    // Get section of data
    casacore::SubLattice<float> sublattice(_lattice, section);
    casacore::ArrayLattice<bool> mask_lattice(sublattice.shape());
    // Set up iterators
    unsigned int max_pix(sublattice.advisedMaxPixels());
    casacore::IPosition cursor_shape(sublattice.doNiceCursorShape(max_pix));
    casacore::RO_LatticeIterator<float> lattice_iter(sublattice, cursor_shape);
    casacore::LatticeIterator<bool> mask_iter(mask_lattice, cursor_shape);
    mask_iter.reset();

    // Retrieve each iteration and set mask
    casacore::Array<bool> mask_cursor_data; // cursor array for each iteration
    for (lattice_iter.reset(); !lattice_iter.atEnd(); lattice_iter++) {
        mask_cursor_data.resize();
        mask_cursor_data = isFinite(lattice_iter.cursor());
        // make sure mask data is same shape as mask cursor
        casacore::IPosition mask_cursor_shape(mask_iter.rwCursor().shape());
        if (mask_cursor_data.shape() != mask_cursor_shape)
            mask_cursor_data.resize(mask_cursor_shape, true);
        mask_iter.rwCursor() = mask_cursor_data;
        mask_iter++;
    }

    buffer = mask_lattice.asArray();
    return true;
}

// Set up image CoordinateSystem, ImageInfo

void CartaHdf5Image::SetUpImage() {
    // Set up coordinate system, image info, misc info from image header entries
    try {
        // convert header entries to FITS header strings
        // Convert specified Hdf5 attributes to FITS-format strings.
        casacore::CountedPtr<casacore::HDF5Group> hdf5_group(_lattice.group());
        Hdf5Attributes::ReadAttributes(hdf5_group.get()->getHid(), _fits_header_strings);
        if (!_fits_header_strings.empty()) {
            // extract HDF5 headers for MiscInfo
            casacore::String schema, converter, converter_version, date;
            for (auto& header : _fits_header_strings) {
                std::vector<std::string> kw_value;
                if (header.contains("SCHEMA_VERSION")) {
                    SplitString(header, '=', kw_value);
                    schema = kw_value[1];
                    schema.trim();
                    schema.gsub("'", "");
                } else if (header.contains("HDF5_CONVERTER=")) {
                    SplitString(header, '=', kw_value);
                    converter = kw_value[1];
                    converter.trim();
                    converter.gsub("'", "");
                } else if (header.contains("HDF5_CONVERTER_VERSION")) {
                    SplitString(header, '=', kw_value);
                    converter_version = kw_value[1];
                    converter_version.trim();
                    converter_version.gsub("'", "");
                } else if (header.contains("DATE")) {
                    SplitString(header, '=', kw_value);
                    date = kw_value[1];
                    date.trim();
                    date.gsub("'", "");
                }
            }

            // set coordinate system
            int stokes_fits_value(1);
            casacore::Record unused_headers_rec;
            casacore::LogSink sink; // null sink; hide confusing FITS log messages
            casacore::LogIO log(sink);
            unsigned int which_rep(0);
            casacore::IPosition image_shape(shape());
            bool drop_stokes(true);
            casacore::CoordinateSystem coordinate_system = casacore::ImageFITSConverter::getCoordinateSystem(
                stokes_fits_value, unused_headers_rec, _fits_header_strings, log, which_rep, image_shape, drop_stokes);
            setCoordinateInfo(coordinate_system);

            // set image units
            setUnits(casacore::ImageFITSConverter::getBrightnessUnit(unused_headers_rec, log));

            // set image info
            casacore::ImageInfo image_info = casacore::ImageFITSConverter::getImageInfo(unused_headers_rec);
            if (stokes_fits_value != -1) {
                casacore::ImageInfo::ImageTypes type = casacore::ImageInfo::imageTypeFromFITS(stokes_fits_value);
                if (type != casacore::ImageInfo::Undefined) {
                    image_info.setImageType(type);
                }
            }
            setImageInfo(image_info);

            // set misc info
            casacore::Record misc_info;
            casacore::ImageFITSConverter::extractMiscInfo(misc_info, unused_headers_rec);
            // shorten keyword names to FITS length 8
            if (!schema.empty()) {
                misc_info.define("h5schema", schema);
            }
            if (!converter.empty()) {
                misc_info.define("h5cnvrtr", converter);
            }
            if (!converter_version.empty()) {
                misc_info.define("h5convsn", converter_version);
            }
            if (!date.empty()) {
                misc_info.define("h5date", date);
            }
            misc_info.removeField("simple"); // remove redundant header
            setMiscInfo(misc_info);
        }
    } catch (casacore::AipsError& err) {
        spdlog::error("Error opening HDF5 image: {}", err.getMesg());
        throw(casacore::AipsError("Error opening HDF5 image"));
    }
}

casacore::Vector<casacore::String> CartaHdf5Image::FitsHeaderStrings() {
    return _fits_header_strings;
}

casacore::uInt CartaHdf5Image::advisedMaxPixels() const {
    return _lattice.advisedMaxPixels();
}

casacore::IPosition CartaHdf5Image::doNiceCursorShape(casacore::uInt maxPixels) const {
    return _lattice.niceCursorShape(maxPixels);
}
