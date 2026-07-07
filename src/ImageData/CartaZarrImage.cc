/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/images/Images/ImageInfo.h>
#include <spdlog/spdlog.h>

#include "ZarrImage.h"

namespace carta {

CartaZarrImage::CartaZarrImage(const std::string& filename)
    : _zarr_image(std::make_shared<ZarrImage>(filename)), _name(filename) {
    if (!_zarr_image->Initialize()) {
        throw casacore::AipsError("Failed to initialize Zarr metadata for: " + filename);
    }

    _shape = _zarr_image->GetShape();

    SetUpImage();

    spdlog::debug("CartaZarrImage created: {} with shape {}", filename, _shape.toString());
}

CartaZarrImage::CartaZarrImage(const CartaZarrImage& other)
    : casacore::ImageInterface<float>(other),
      _zarr_image(other._zarr_image),
      _shape(other._shape),
      _name(other._name) {
    spdlog::debug("CartaZarrImage copy constructor: sharing metadata for {}", _name);
}

CartaZarrImage::~CartaZarrImage() {}

casacore::String CartaZarrImage::imageType() const {
    return "CartaZarrImage";
}

casacore::String CartaZarrImage::name(casacore::Bool stripPath) const {
    if (stripPath) {
        casacore::Path path(_name);
        return path.baseName();
    }
    return _name;
}

casacore::IPosition CartaZarrImage::shape() const {
    return _shape;
}

casacore::Bool CartaZarrImage::ok() const {
    return _zarr_image && _zarr_image->IsInitialized();
}

casacore::DataType CartaZarrImage::dataType() const {
    return casacore::TpFloat;
}

casacore::DataType CartaZarrImage::InternalDataType() const {
    return _zarr_image->GetDataType();
}

casacore::Vector<casacore::String> CartaZarrImage::FitsHeaderStrings() {
    if (!_fits_header_strings.empty()) {
        return _fits_header_strings;
    }

    _fits_header_strings = _zarr_image->FitsHeaderStrings();
    return _fits_header_strings;
}

std::vector<std::pair<std::string, std::string>> CartaZarrImage::GetStorageInfo() {
    return _zarr_image->GetStorageInfo();
}

casacore::Bool CartaZarrImage::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    throw casacore::AipsError("CartaZarrImage::doGetSlice - Pixel data reading is not implemented yet");
}

void CartaZarrImage::doPutSlice(
    const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) {
    throw casacore::AipsError("CartaZarrImage::doPutSlice - Image is not writable");
}

const casacore::LatticeRegion* CartaZarrImage::getRegionPtr() const {
    return nullptr;
}

void CartaZarrImage::resize(const casacore::TiledShape& newShape) {
    throw casacore::AipsError("CartaZarrImage::resize - Image is not writable");
}

casacore::ImageInterface<float>* CartaZarrImage::cloneII() const {
    return new CartaZarrImage(*this);
}

casacore::Bool CartaZarrImage::isMasked() const {
    return false;
}

casacore::Bool CartaZarrImage::hasPixelMask() const {
    return false;
}

const casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() const {
    throw casacore::AipsError("CartaZarrImage::pixelMask - No pixel mask");
}

casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() {
    throw casacore::AipsError("CartaZarrImage::pixelMask - No pixel mask");
}

casacore::Bool CartaZarrImage::doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) {
    throw casacore::AipsError("CartaZarrImage::doGetMaskSlice - Mask data reading is not implemented yet");
}

void CartaZarrImage::SetUpImage() {
    try {
        casacore::Vector<casacore::String> header_strings = FitsHeaderStrings();

        if (!header_strings.empty()) {
            int stokes_fits_value(1);
            casacore::Record unused_headers;
            casacore::LogSink sink; // null sink to suppress confusing FITS log messages
            casacore::LogIO log(sink);
            unsigned int which_rep(0);
            bool drop_stokes(false);

            casacore::CoordinateSystem coord_sys = casacore::ImageFITSConverter::getCoordinateSystem(
                stokes_fits_value, unused_headers, header_strings, log, which_rep, _shape, drop_stokes);

            // The FITS header represents the Stokes axis linearly (CRVAL4/CDELT4), which cannot
            // describe a non-uniform polarization ordering. Replace it with a StokesCoordinate built
            // directly from the per-plane polarization list, which supports arbitrary ordering.
            casacore::Vector<casacore::Int> stokes_types = _zarr_image->GetStokesTypes();
            int pol_coord = coord_sys.polarizationCoordinateNumber();
            if (!stokes_types.empty() && pol_coord >= 0) {
                casacore::StokesCoordinate stokes_coord(stokes_types);
                coord_sys.replaceCoordinate(stokes_coord, pol_coord);
            }

            setCoordinateInfo(coord_sys);

            setUnits(casacore::ImageFITSConverter::getBrightnessUnit(unused_headers, log));

            casacore::ImageInfo image_info = casacore::ImageFITSConverter::getImageInfo(unused_headers);
            if (stokes_fits_value != -1) {
                casacore::ImageInfo::ImageTypes type = casacore::ImageInfo::imageTypeFromFITS(stokes_fits_value);
                if (type != casacore::ImageInfo::Undefined) {
                    image_info.setImageType(type);
                }
            }
            setImageInfo(image_info);

            casacore::Record misc_info;
            casacore::ImageFITSConverter::extractMiscInfo(misc_info, unused_headers);
            setMiscInfo(misc_info);

            SetBeams();

            spdlog::debug("CartaZarrImage::SetUpImage - Successfully set up image from FITS header");
            return;
        }
        throw casacore::AipsError("Zarr image has no FITS header for coordinate system setup");
    } catch (casacore::AipsError& err) {
        spdlog::error("Error opening Zarr image: {}", err.getMesg());
        throw(casacore::AipsError("Error opening Zarr image"));
    }
}

void CartaZarrImage::SetBeams() {
    casacore::ImageBeamSet beam_set;
    if (_zarr_image->GetBeams(beam_set)) {
        spdlog::debug("CartaZarrImage::SetBeams - Applying beam set");
        casacore::ImageInfo info = imageInfo();
        info.setBeams(beam_set);
        setImageInfo(info);
    }
}

} // namespace carta
