/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaFitsImage.cc : specialized casacore::ImageInterface implementation for FITS

#include "CartaFitsImage.h"

#include <casacore/casa/BasicSL/Constants.h>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/FITSCoordinateUtil.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/Stokes.h>
#include <casacore/tables/DataMan/TiledFileAccess.h>

#include <wcslib/fitshdr.h>
#include <wcslib/wcs.h>
#include <wcslib/wcsfix.h>
#include <wcslib/wcshdr.h>
#include <wcslib/wcsmath.h>

using namespace carta;

CartaFitsImage::CartaFitsImage(const std::string& filename, unsigned int hdu)
    : casacore::ImageInterface<float>(),
      _filename(filename),
      _hdu(hdu),
      _fptr(nullptr),
      _is_compressed(false),
      _bitpix(-32),       // assume float
      _equiv_bitpix(-32), // assume float
      _has_blanks(false),
      _pixel_mask(nullptr),
      _is_copy(false) {
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
      _fptr(other._fptr),
      _shape(other._shape),
      _is_compressed(other._is_compressed),
      _bitpix(other._bitpix),
      _equiv_bitpix(other._equiv_bitpix),
      _has_blanks(other._has_blanks),
      _pixel_mask(nullptr),
      _tiled_shape(other._tiled_shape),
      _is_copy(true) {
    if (other._pixel_mask != nullptr) {
        _pixel_mask = other._pixel_mask->clone();
    }
}

CartaFitsImage::~CartaFitsImage() {
    if (!_is_copy) {
        CloseFile();
        delete _pixel_mask;
    }
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
    if (bitpix_types.find(_equiv_bitpix) != bitpix_types.end()) {
        return bitpix_types.at(_equiv_bitpix);
    }

    return casacore::DataType::TpFloat; // casacore FITSImage default
}

casacore::DataType CartaFitsImage::internalDataType() const {
    if (bitpix_types.find(_bitpix) != bitpix_types.end()) {
        return bitpix_types.at(_bitpix);
    }

    return casacore::DataType::TpFloat; // casacore FITSImage default
}

casacore::Bool CartaFitsImage::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    // Read section of data using cfitsio implicit data type conversion.
    // cfitsio scales the data by BSCALE and BZERO
    fitsfile* fptr = OpenFile();

    // Read data subset from image
    bool ok(false);
    switch (_equiv_bitpix) {
        case 8: {
            ok = GetDataSubset<unsigned char>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
        case 16: {
            ok = GetDataSubset<short>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
        case 32: {
            ok = GetDataSubset<int>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
        case 64: {
            ok = GetDataSubset<LONGLONG>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
        case -32: {
            ok = GetDataSubset<float>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
        case -64: {
            ok = GetDataSubset<double>(fptr, _equiv_bitpix, section, buffer);
            break;
        }
    }

    if (!ok) {
        spdlog::error("FITS read data failed.");
        return false;
    }

    return true;
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
    return _tiled_shape.tileShape().product();
}

casacore::IPosition CartaFitsImage::doNiceCursorShape(casacore::uInt) const {
    return _tiled_shape.tileShape();
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

    return pixelMask();
}

casacore::Lattice<bool>& CartaFitsImage::pixelMask() {
    if (!_has_blanks) {
        throw(casacore::AipsError("CartaFitsImage::pixelMask - no pixel mask used"));
    }

    if (!_pixel_mask) {
        SetPixelMask();
    }

    return *_pixel_mask;
}

casacore::Bool CartaFitsImage::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    // Return slice of stored pixel mask
    if (!_has_blanks) {
        buffer.resize(section.length());
        buffer = true;
        return false;
    }

    if (!_pixel_mask) {
        if (_bitpix > 0) {
            SetPixelMask();
        } else {
            return doGetNanMaskSlice(buffer, section);
        }
    }

    if (_pixel_mask) {
        return _pixel_mask->getSlice(buffer, section);
    }

    return false;
}

// private

fitsfile* CartaFitsImage::OpenFile() {
    // Open file and return file pointer
    if (!_fptr) {
        fitsfile* fptr;
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
        CloseFileIfError(status, "Error advancing FITS file to requested HDU.");

        _fptr = fptr;
    }

    return _fptr;
}

void CartaFitsImage::CloseFile() {
    if (_fptr) {
        int status = 0;
        fits_close_file(_fptr, &status);
        _fptr = nullptr;
    }
}

void CartaFitsImage::CloseFileIfError(const int& status, const std::string& error) {
    // Close file if cfitsio status is not 0 (ok).  If error, throw exception.
    if (status) {
        CloseFile();

        if (!error.empty()) {
            throw(casacore::AipsError(error));
        }
    }
}

void CartaFitsImage::SetUpImage() {
    // Set up image parameters and coordinate system from headers
    // Read headers into string
    int nheaders(0);
    std::string header;
    GetFitsHeaderString(nheaders, header);

    // Headers as String vector to pass to converter
    SetFitsHeaderStrings(nheaders, header);

    casacore::Record unused_headers;
    casacore::LogSink sink;
    casacore::LogIO log(sink);
    casacore::CoordinateSystem coord_sys;
    int stokes_fits_value(1);
    bool drop_stokes(true);

    try {
        // Set coordinate system
        coord_sys = casacore::ImageFITSConverter::getCoordinateSystem(
            stokes_fits_value, unused_headers, _image_header_strings, log, 0, _shape, drop_stokes);
    } catch (const casacore::AipsError& err) {
        if (err.getMesg().startsWith("TabularCoordinate")) {
            // Spectral axis defined in velocity fails if no rest freq to convert to frequencies
            try {
                // Set up with wcslib
                coord_sys = SetCoordinateSystem(nheaders, header, unused_headers, stokes_fits_value);
            } catch (const casacore::AipsError& err) {
                spdlog::debug("Coordinate system setup error: {}", err.getMesg());
                throw(casacore::AipsError("Coordinate system setup from FITS headers failed."));
            }
        } else {
            spdlog::debug("Coordinate system setup error: {}", err.getMesg());
            throw(casacore::AipsError("Coordinate system setup from FITS headers failed."));
        }
    }

    try {
        // Set tiled shape for data access (must be done before image info in case of multiple beams)
        _tiled_shape = casacore::TiledShape(_shape, casacore::TiledFileAccess::makeTileShape(_shape));

        // Set coord sys in image
        setCoordinateInfo(coord_sys);

        // Set image units
        setUnits(casacore::ImageFITSConverter::getBrightnessUnit(unused_headers, log));

        // Set image info
        casacore::ImageInfo image_info = casacore::ImageFITSConverter::getImageInfo(unused_headers);
        if (stokes_fits_value != -1) {
            casacore::ImageInfo::ImageTypes type = casacore::ImageInfo::imageTypeFromFITS(stokes_fits_value);
            if (type != casacore::ImageInfo::Undefined) {
                image_info.setImageType(type);
            }
        }

        if (unused_headers.isDefined("casambm") && unused_headers.asRecord("casambm").asBool("value")) {
            ReadBeamsTable(image_info);
        }

        setImageInfo(image_info);

        // Set misc info
        casacore::Record misc_info;
        casacore::ImageFITSConverter::extractMiscInfo(misc_info, unused_headers);
        setMiscInfo(misc_info);

    } catch (const casacore::AipsError& err) {
        spdlog::debug("Image setup error: {}", err.getMesg());
        throw(casacore::AipsError("Image setup from FITS headers failed."));
    }
}

void CartaFitsImage::GetFitsHeaderString(int& nheaders, std::string& hdrstr) {
    // Read header values into single string, and store some image parameters.
    // Returns string and number of keys contained in string.
    // Throws exception if any headers missing.
    fitsfile* fptr = OpenFile();
    int status(0);

    // Check hdutype
    if (_hdu > 0) {
        int hdutype(-1);
        fits_get_hdu_type(fptr, &hdutype, &status); // IMAGE_HDU, ASCII_TBL, BINARY_TBL
        CloseFileIfError(status, "Error determining HDU type.");

        if (hdutype != IMAGE_HDU) {
            CloseFileIfError(1, "No image at specified hdu in FITS file.");
        }
    }

    // Get image parameters: BITPIX, NAXIS, NAXISn
    int maxdim(4), bitpix(0), naxis(0);
    long naxes[maxdim];
    status = 0;
    fits_get_img_param(fptr, maxdim, &bitpix, &naxis, naxes, &status);
    CloseFileIfError(status, "Error getting image parameters.");

    if (naxis < 2) {
        CloseFileIfError(1, "Image must be at least 2D.");
    }

    // Set data type and shape
    _bitpix = bitpix;
    _shape.resize(naxis);
    for (int i = 0; i < naxis; ++i) {
        _shape(i) = naxes[i];
    }

    // Equivalent data type for scaled data
    int equiv_bitpix;
    status = 0;
    fits_get_img_equivtype(fptr, &equiv_bitpix, &status);
    if (status) {
        _equiv_bitpix = _bitpix;
    } else {
        _equiv_bitpix = equiv_bitpix;
    }

    // Set blanks used for integer datatypes (int value for NAN), for pixel mask
    if (bitpix > 0) {
        std::string key("BLANK");
        int blank;
        char* comment(nullptr); // ignore
        status = 0;
        fits_read_key(fptr, TINT, key.c_str(), &blank, comment, &status);
        _has_blanks = !status;
    } else {
        // For float (-32) and double (-64) mask is represented by NaN
        _has_blanks = true;
    }

    // Get headers to set up image:
    // Previous functions converted compressed headers automatically; must call specific function for uncompressed headers
    // Determine whether tile compressed
    status = 0;
    _is_compressed = fits_is_compressed_image(fptr, &status);
    CloseFileIfError(status, "Error detecting image compression.");

    // Number of headers (keys).  nkeys is function parameter.
    nheaders = 0;
    int* more_keys(nullptr);
    status = 0;
    fits_get_hdrspace(fptr, &nheaders, more_keys, &status);
    CloseFileIfError(status, "Unable to determine FITS headers.");

    // Get headers as single string with no exclusions (exclist=nullptr, nexc=0)
    int no_comments(0);
    char* header[nheaders];
    status = 0;
    if (_is_compressed) {
        // Convert to uncompressed headers
        fits_convert_hdr2str(fptr, no_comments, nullptr, 0, header, &nheaders, &status);
    } else {
        fits_hdr2str(fptr, no_comments, nullptr, 0, header, &nheaders, &status);
    }

    if (status) {
        // Free memory allocated by cfitsio, close file, throw exception
        int free_status(0);
        fits_free_memory(*header, &free_status);
        CloseFileIfError(status, "Unable to read FITS headers.");
    }

    hdrstr = std::string(header[0]);

    // Free memory allocated by cfitsio
    int free_status(0);
    fits_free_memory(*header, &free_status);

    // Done with file
    CloseFile();
}

void CartaFitsImage::SetFitsHeaderStrings(int nheaders, const std::string& header) {
    // Set header strings as vector of 80-char strings, with and without history headers.
    _all_header_strings.resize(nheaders);
    std::vector<casacore::String> no_history_strings;
    size_t pos(0);

    for (int i = 0; i < nheaders; ++i) {
        casacore::String hstring = header.substr(pos, 80);
        _all_header_strings(i) = hstring;

        if (!hstring.startsWith("HISTORY")) {
            no_history_strings.push_back(hstring);
        }

        pos += 80;
    }

    // For setting up image
    _image_header_strings = no_history_strings;
}

casacore::Vector<casacore::String> CartaFitsImage::FitsHeaderStrings() {
    // Return all headers as string vector
    if (_all_header_strings.empty()) {
        // Headers as single string
        int nheaders(0);
        std::string fits_headers;
        GetFitsHeaderString(nheaders, fits_headers);

        // Headers as vector of strings
        SetFitsHeaderStrings(nheaders, fits_headers);
    }

    return _all_header_strings;
}

casacore::CoordinateSystem CartaFitsImage::SetCoordinateSystem(
    int nheaders, const std::string& header_str, casacore::RecordInterface& unused_headers, int& stokes_fits_value) {
    // Sets up coordinate sys with wcslib due to error thrown by casacore, or throws exception.
    // Based on casacore::FITSCoordinateUtil::fromFITSHeader.
    //   Unfortunately, only this top-level function is public,
    //   so if any coordinate "fails" the entire coordinate system fails.
    //   It is impossible to reuse the code for the "good" coordinates.

    // Parse header string into wcsprm struct; removes used keyrecords
    // inputs:
    char header[header_str.size()];
    std::strcpy(header, header_str.c_str());
    int relax(WCSHDR_all); // allow informal extensions of WCS standard
    int ctrl(-2);          // report rejected keys and remove
    // outputs:
    int nreject(0); // number of rejected keys
    int nwcs(0);    // number of coordinate representations found
    ::wcsprm* wcs_ptr(nullptr);

    int status = wcspih(header, nheaders, relax, ctrl, &nreject, &nwcs, &wcs_ptr);
    if (status || (nwcs == 0)) {
        spdlog::debug("Coordinate system error: wcslib parser error");
        throw(casacore::AipsError("Coordinate system setup failed."));
    }

    // wcsfix: translate non-standard wcs usage, e.g. projection types, spectral types, etc.
    ctrl = 7; // do all correction functions
    std::vector<int> tmpshp(_shape.begin(), _shape.end());
    int stat[NWCSFIX]; // 7; status of all functions
    status = wcsfix(ctrl, &(tmpshp[0]), &wcs_ptr[0], stat);

    if (status) {
        wcsvfree(&nwcs, &wcs_ptr);
        spdlog::debug("Coordinate system error: wcslib fix error");
        throw(casacore::AipsError("Coordinate system setup failed."));
    }

    // Put unused keyrecords in header string into Record
    SetHeaderRec(header, unused_headers);

    casacore::UnitMap::addFITS(); // add FITS units for Quanta

    // Add ObsInfo and remove used keyrecords from Record
    casacore::CoordinateSystem coord_sys;
    AddObsInfo(coord_sys, unused_headers);

    // Determine the coordinates (longitude, latitude, spectral, stokes axes)
    int long_axis(-1), lat_axis(-1), spec_axis(-1), stokes_axis(-1), lin_spec_axis(-1);

    const ::wcsprm wcs0 = wcs_ptr[0];
    const unsigned int naxes = wcs0.naxis;

    // Direction coordinate
    std::vector<int> dir_axes;
    bool ok = AddDirectionCoordinate(coord_sys, wcs0, dir_axes);

    if (!ok) {
        wcsvfree(&nwcs, &wcs_ptr);
        throw(casacore::AipsError("Direction coordinate setup failed."));
    }

    if (dir_axes.size() == 2) {
        long_axis = dir_axes[0];
        lat_axis = dir_axes[1];
    }

    // Stokes coordinate
    ok = AddStokesCoordinate(coord_sys, wcs0, _shape, stokes_fits_value, stokes_axis);
    if (!ok) {
        wcsvfree(&nwcs, &wcs_ptr);
        throw(casacore::AipsError("Stokes coordinate setup failed."));
    }

    // Spectral coordinate
    ok = AddSpectralCoordinate(coord_sys, wcs0, _shape, spec_axis, lin_spec_axis);
    if (!ok) {
        wcsvfree(&nwcs, &wcs_ptr);
        throw(casacore::AipsError("Spectral coordinate setup failed."));
    }

    // Linear coordinate
    std::vector<int> lin_axes;
    ok = AddLinearCoordinate(coord_sys, wcs0, lin_axes);

    // Free wcs memory
    wcsvfree(&nwcs, &wcs_ptr);

    if (!ok) {
        throw(casacore::AipsError("Linear coordinate setup failed."));
    }

    // Set order of coordinate system with special axes first
    std::vector<int> special_axes = {long_axis, lat_axis, spec_axis, stokes_axis, lin_spec_axis};
    SetCoordSysOrder(coord_sys, naxes, special_axes, lin_axes);

    return coord_sys;
}

bool CartaFitsImage::AddDirectionCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& direction_axes) {
    // Create casacore::DirectionCoordinate from headers and add to coord_sys.
    // Returns index of direction axes in vector and whether wcssub (coordinate extraction) succeeded.

    // Initialize LAT/LONG wcs structure
    int nsub(2);
    ::wcsprm wcs_long_lat;
    wcs_long_lat.flag = -1;
    int status = wcsini(1, nsub, &wcs_long_lat);
    if (status) {
        return false;
    }

    // Extract LAT/LONG wcs structure
    casacore::Block<int> axes(nsub);
    axes[0] = WCSSUB_LONGITUDE;
    axes[1] = WCSSUB_LATITUDE;

    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_long_lat);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_long_lat);
        return false;
    }

    bool ok(true);
    if (nsub == 2) {
        // Found 2 direction coordinates, should be able to discern direction system.
        // Set direction axes
        direction_axes.push_back(axes[0] - 1); // 1-based to 0-based
        direction_axes.push_back(axes[1] - 1); // 1-based to 0-based

        // Set up wcsprm struct with wcsset
        casacore::Coordinate::set_wcs(wcs_long_lat);

        // Set direction system for DirectionCoordinate constructor using CTYPE, EQUINOX, RADESYS
        casacore::String ctype1 = wcs_long_lat.ctype[0];
        casacore::String ctype2 = wcs_long_lat.ctype[1];
        ctype1.upcase();
        ctype2.upcase();

        bool have_equinox = !undefined(wcs_long_lat.equinox);
        double equinox(0.0);
        if (have_equinox) {
            equinox = wcs_long_lat.equinox;
        }
        bool equinox_is_2000 = casacore::near(equinox, 2000.0);

        casacore::MDirection::Types direction_type;
        bool dir_type_defined(false); // No "unknown" type

        // Check LAT/LON direction system
        if ((ctype1.find("GLON") != std::string::npos) && (ctype2.find("GLAT") != std::string::npos)) {
            direction_type = casacore::MDirection::GALACTIC;
            dir_type_defined = true;
        } else if ((ctype1.find("SLON") != std::string::npos) && (ctype2.find("SLAT") != std::string::npos)) {
            direction_type = casacore::MDirection::SUPERGAL;
            dir_type_defined = true;
        } else if ((ctype1.find("ELON") != std::string::npos) && (ctype2.find("ELAT") != std::string::npos)) {
            if (!have_equinox || equinox_is_2000) {
                direction_type = casacore::MDirection::ECLIPTIC;
                dir_type_defined = true;
            }
        } else if (((ctype1.find("LON") != std::string::npos) || (ctype1.find("LAT") != std::string::npos)) &&
                   ((ctype2.find("LON") != std::string::npos) || (ctype2.find("LAT") != std::string::npos))) {
            spdlog::debug("{} and {} are unsupported types", ctype1, ctype2);
        } else {
            // Not LAT/LON
            std::string radesys(wcs_long_lat.radesys);
            bool equinox_is_1950 = casacore::near(equinox, 1950.0);
            bool equinox_is_1950_vla = casacore::near(equinox, 1979.9);

            if (radesys[0] != '\0') {
                // Use RADESYS for direction type
                if (radesys.find("ICRS") != std::string::npos) {
                    if (!have_equinox || equinox_is_2000) {
                        direction_type = casacore::MDirection::ICRS;
                        dir_type_defined = true;
                    }
                } else if (radesys.find("FK5") != std::string::npos) {
                    if (!have_equinox || equinox_is_2000) {
                        direction_type = casacore::MDirection::J2000;
                        dir_type_defined = true;
                    }
                } else if (radesys.find("FK4") != std::string::npos) {
                    if (!have_equinox || equinox_is_1950) {
                        direction_type = casacore::MDirection::B1950;
                        dir_type_defined = true;
                    } else if (!have_equinox || equinox_is_1950_vla) {
                        direction_type = casacore::MDirection::B1950_VLA;
                        dir_type_defined = true;
                    }
                } else if (radesys.find("GAPPT") != std::string::npos) {
                    spdlog::debug("RADESYS GAPPT not supported");
                }
            } else if (have_equinox) {
                // Use EQUINOX
                if (equinox >= 1984.0) {
                    direction_type = casacore::MDirection::J2000;
                } else if (equinox_is_1950_vla) {
                    direction_type = casacore::MDirection::B1950_VLA;
                } else {
                    direction_type = casacore::MDirection::B1950;
                }
                dir_type_defined = true;
            } else {
                spdlog::debug("Direction system not defined, assuming J2000.");
                direction_type = casacore::MDirection::J2000;
                dir_type_defined = true;
            }
        }

        if (dir_type_defined) {
            try {
                casacore::DirectionCoordinate direction_coord(direction_type, wcs_long_lat, true);
                coord_sys.addCoordinate(direction_coord);
            } catch (const casacore::AipsError& err) {
                // Catch so the wcs sub can be freed
                ok = false;
            }
        } else {
            // If nsub is 2, should have been able to determine direction type
            ok = false;
        }
    }

    wcsfree(&wcs_long_lat);
    return ok;
}

bool CartaFitsImage::AddStokesCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, const casacore::IPosition& shape,
    int& stokes_fits_value, int& stokes_axis) {
    // Create casacore::StokesCoordinate from headers and add to coord_sys.
    // Returns stokes axis and whether wcssub (coordinate extraction) succeeded.

    // Initialize STOKES wcs structure
    int nsub(1);
    ::wcsprm wcs_stokes;
    wcs_stokes.flag = -1;
    int status = wcsini(1, nsub, &wcs_stokes);
    if (status) {
        return false;
    }

    // Extract STOKES wcs structure
    casacore::Block<int> axes(nsub);
    axes[0] = WCSSUB_STOKES;
    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_stokes);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_stokes);
        return false;
    }

    bool ok(true);
    if (nsub == 1) {
        // Found 1 stokes axis
        stokes_axis = axes[0] - 1; // 0-based

        size_t stokes_length(1);
        if (stokes_axis < shape.size()) {
            stokes_length = shape(stokes_axis);
            if (stokes_length > 4) {
                spdlog::debug("Stokes coordinate length > 4.");
                wcsfree(&wcs_stokes);
                return false;
            }
        }

        // Set up wcsprm struct with wcsset
        casacore::Coordinate::set_wcs(wcs_stokes);

        // Get FITS header values CRPIX, CRVAL, CDELT
        double crpix = wcs_stokes.crpix[0] - 1.0; // 0-based
        double crval = wcs_stokes.crval[0];
        double cdelt = wcs_stokes.cdelt[0];

        // Used to set image type if not -1, else uses FITS BTYPE header
        stokes_fits_value = -1;

        // Determine stokes type for each index
        casacore::Vector<casacore::Int> stokes_types(stokes_length);

        for (size_t i = 0; i < stokes_length; ++i) {
            double tmp = crval + (i - crpix) * cdelt;
            int tmp_stokes = (tmp >= 0 ? int(tmp + 0.01) : int(tmp - 0.01));

            switch (tmp_stokes) {
                case 0: {
                    spdlog::debug("Detected Stokes coordinate = 0, setting to Undefined.");
                    stokes_types(i) = casacore::Stokes::Undefined;
                    stokes_fits_value = 0; // ImageInfo::Beam
                    break;
                }
                case 5: {
                    spdlog::debug(
                        "Detected Stokes coordinate is unofficial percentage polarization value.  Using fractional polarization instead.");
                    stokes_types(i) = casacore::Stokes::PFlinear;
                    break;
                }
                case 8: {
                    spdlog::debug("Detected Stokes coordinate is unofficial spectral index value, setting to Undefined.");
                    stokes_types(i) = casacore::Stokes::Undefined;
                    stokes_fits_value = 8; // ImageInfo::SpectralIndex
                    break;
                }
                case 9: {
                    spdlog::debug("Detected Stokes coordinate is unofficial optical depth, setting to Undefined.");
                    stokes_types(i) = casacore::Stokes::Undefined;
                    stokes_fits_value = 9; // ImageInfo::OpticalDepth
                    break;
                }
                default: {
                    casacore::Stokes::StokesTypes type = casacore::Stokes::fromFITSValue(tmp_stokes);
                    if (type == casacore::Stokes::Undefined) {
                        spdlog::debug("Detected invalid Stokes coordinate {}, setting to Undefined.", tmp_stokes);
                    }
                    stokes_types(i) = type;
                    break;
                }
            }
        }

        try {
            casacore::StokesCoordinate stokes_coord(stokes_types);
            coord_sys.addCoordinate(stokes_coord);
        } catch (const casacore::AipsError& err) {
            // Catch so the wcs sub can be freed
            ok = false;
        }
    }

    wcsfree(&wcs_stokes);
    return ok;
}

bool CartaFitsImage::AddSpectralCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, const casacore::IPosition& shape,
    int& spectral_axis, int& linear_spectral_axis) {
    // Create casacore::SpectralCoordinate from headers and add to coord_sys.
    // Returns spectral axis and whether wcssub (coordinate extraction) succeeded.

    // Initialize SPECTRAL wcs structure
    int nsub(1);
    ::wcsprm wcs_spectral;
    wcs_spectral.flag = -1;
    int status = wcsini(1, nsub, &wcs_spectral);
    if (status) {
        return false;
    }

    // Extract SPECTRAL wcs structure
    casacore::Block<int> axes(nsub);
    axes[0] = WCSSUB_SPECTRAL;
    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_spectral);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_spectral);
        return false;
    }

    bool ok(true);
    if (nsub == 1) {
        // Found 1 spectral axis
        spectral_axis = axes[0] - 1; // 0-based

        size_t num_chan(1);
        if (spectral_axis < shape.size()) {
            num_chan = shape(spectral_axis);
        }

        if (num_chan == 0) {
            spdlog::debug("Spectral coordinate has no channels.");
            wcsfree(&wcs_spectral);
            return false;
        }

        // CTYPE, native type for SpectralCoordinate
        casacore::String ctype1 = wcs_spectral.ctype[0];
        if (ctype1.startsWith("FREQ")) {
            try {
                casacore::MFrequency::Types frequency_type = GetFrequencyType(wcs_spectral);
                casacore::SpectralCoordinate spectral_coord(frequency_type, wcs);
                spectral_coord.setNativeType(casacore::SpectralCoordinate::FREQ);
                coord_sys.addCoordinate(spectral_coord);
            } catch (const casacore::AipsError& err) {
                spdlog::debug("Failed to set FREQ spectral coordinate from wcs.");
                ok = false;
            }
        } else if (ctype1.startsWith("WAVE") || ctype1.startsWith("AWAV") || ctype1.startsWith("VOPT") || ctype1.startsWith("FELO")) {
            // Set up wcsprm struct with wcsset
            casacore::Coordinate::set_wcs(wcs_spectral);

            // Determine frequency type
            casacore::MFrequency::Types frequency_type = GetFrequencyType(wcs_spectral);

            if (frequency_type == casacore::MFrequency::Undefined) {
                spdlog::debug("Failed to determine spectral reference frame.");
                wcsfree(&wcs_spectral);
                return false;
            }

            // Spectral headers
            double crval = wcs_spectral.crval[0];
            double crpix = wcs_spectral.crpix[0];
            double cdelt = wcs_spectral.cdelt[0];
            double pc = wcs_spectral.pc[0];
            double rest_frequency = wcs_spectral.restfrq;
            double rest_wav = wcs_spectral.restwav;
            casacore::String cunit(wcs_spectral.cunit[0]);

            if (rest_frequency == 0.0) {
                if (wcs_spectral.restwav != 0.0) {
                    rest_frequency = casacore::C::c / wcs.restwav;
                }
            }

            if (ctype1.startsWith("WAVE") || ctype1.startsWith("AWAV")) {
                // Calculate wavelengths
                casacore::Vector<casacore::Double> wavelengths(num_chan);
                for (size_t i = 0; i < num_chan; ++i) {
                    wavelengths(i) = crval + (cdelt * pc * (double(i + 1) - crpix)); // +1 because FITS is 1-based
                }

                bool in_air(false);
                casacore::SpectralCoordinate::SpecType native_type(casacore::SpectralCoordinate::WAVE);

                if (ctype1.contains("AWAV")) {
                    in_air = true;
                    native_type = casacore::SpectralCoordinate::AWAV;
                }

                try {
                    casacore::SpectralCoordinate spectral_coord(frequency_type, wavelengths, cunit, rest_frequency, in_air);
                    spectral_coord.setNativeType(native_type);
                    coord_sys.addCoordinate(spectral_coord);
                } catch (const casacore::AipsError& err) {
                    // Catch so the wcs sub can be freed
                    ok = false;
                }
            } else {
                // Calculate velocities for VOPT, frequencies for FELO
                casacore::Vector<casacore::Double> values(num_chan);
                for (size_t i = 0; i < num_chan; ++i) {
                    double vel = crval + (cdelt * pc * (double(i + 1) - crpix));

                    if (ctype1.contains("VOPT")) {
                        values(i) = vel;
                    } else {
                        casacore::Unit vel_unit(cunit);
                        casacore::Quantity vel_quant(vel, vel_unit);
                        double vel_mps = vel_quant.getValue("m/s");
                        if (vel_mps > -casacore::C::c) {
                            values(i) = rest_frequency / ((vel_mps / casacore::C::c) + 1.0); // in Hz
                        } else {
                            values(i) = HUGE_VAL;
                        }
                    }
                }

                if (ctype1.contains("VOPT")) {
                    try {
                        // Construct with velocities in km/s
                        casacore::MDoppler::Types doppler_type(casacore::MDoppler::OPTICAL);
                        casacore::SpectralCoordinate spectral_coord(frequency_type, doppler_type, values, cunit, rest_frequency);
                        spectral_coord.setNativeType(casacore::SpectralCoordinate::VOPT);
                        coord_sys.addCoordinate(spectral_coord);
                    } catch (const casacore::AipsError& err) {
                        if (err.getMesg().contains("TabularCoordinate") && (rest_frequency == 0.0)) {
                            // TabularCoordinate for frequencies fails if no rest frequency.
                            // Create LinearCoordinate for velocities
                            bool one_based(true);
                            casacore::LinearCoordinate linear_coord(wcs_spectral, one_based);
                            coord_sys.addCoordinate(linear_coord);
                            linear_spectral_axis = spectral_axis;
                            spectral_axis = -1;
                        } else {
                            ok = false;
                        }
                    }
                } else {
                    try {
                        // Construct with frequencies in Hz
                        casacore::SpectralCoordinate spectral_coord(frequency_type, values, rest_frequency);
                        spectral_coord.setNativeType(casacore::SpectralCoordinate::VOPT);
                        coord_sys.addCoordinate(spectral_coord);
                    } catch (const casacore::AipsError& err) {
                        // Catch so the wcs sub can be freed
                        ok = false;
                    }
                }
            }
        } else {
            casacore::SpectralCoordinate::SpecType native_type;
            if (ctype1.startsWith("VELO")) {
                native_type = casacore::SpectralCoordinate::VRAD;
            } else if (ctype1.startsWith("VRAD")) {
                native_type = casacore::SpectralCoordinate::VRAD;
            } else if (ctype1.startsWith("BETA")) {
                native_type = casacore::SpectralCoordinate::BETA;
            } else {
                spdlog::debug("Spectral coordinate type {} not supported.", ctype1);
                wcsfree(&wcs_spectral);
                return false;
            }

            // Translate spectral axis to FREQ
            int spectral_axis_index(0); // in wcs_spectral struct
            char ctype[9];
            strcpy(ctype, "FREQ-???");
            int status = wcssptr(&wcs_spectral, &spectral_axis_index, ctype);

            if (status) {
                switch (status) {
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        break;
                    default:
                        ok = false;
                }
            } else {
                // Set up wcsprm struct with wcsset
                casacore::Coordinate::set_wcs(wcs_spectral);
            }

            if (ok) {
                // Determine frequency type
                casacore::MFrequency::Types frequency_type = GetFrequencyType(wcs_spectral);

                if (frequency_type == casacore::MFrequency::Undefined) {
                    spdlog::debug("Failed to determine spectral reference frame.");
                    wcsfree(&wcs_spectral);
                    return false;
                }

                try {
                    bool one_based(true);
                    casacore::SpectralCoordinate spectral_coord(frequency_type, wcs_spectral, one_based);
                    spectral_coord.setNativeType(native_type);
                    coord_sys.addCoordinate(spectral_coord);
                } catch (const casacore::AipsError& err) {
                    // Catch so the wcs sub can be freed
                    ok = false;
                }
            }
        }
    }

    wcsfree(&wcs_spectral);
    return ok;
}

casacore::MFrequency::Types CartaFitsImage::GetFrequencyType(const ::wcsprm& wcs_spectral) {
    // Determine frequency type from wcs spectral headers
    casacore::MFrequency::Types freq_type(casacore::MFrequency::Undefined);

    if (wcs_spectral.specsys[0] == '\0') {
        // If no SPECSYS, use VELREF
        if (wcs_spectral.velref == 0) {
            // No VELREF either, freq type undefined
            return freq_type;
        } else {
            int velref = wcs_spectral.velref;
            if (velref > 256) {
                velref -= 256;
            }

            std::vector<casacore::MFrequency::Types> velref_freq_types = {casacore::MFrequency::LSRK, casacore::MFrequency::BARY,
                casacore::MFrequency::TOPO, casacore::MFrequency::LSRD, casacore::MFrequency::GEO, casacore::MFrequency::REST,
                casacore::MFrequency::GALACTO};
            if ((velref > 0) && (velref - 1) < velref_freq_types.size()) {
                freq_type = velref_freq_types[velref - 1];
            } else {
                spdlog::debug("Frequency type from VELREF undefined by AIPS convention.  TOPO assumed.");
                freq_type = casacore::MFrequency::TOPO;
            }

            return freq_type;
        }
    }

    // Use SPECSYS
    casacore::String specsys(wcs_spectral.specsys);
    specsys.upcase();

    std::unordered_map<std::string, casacore::MFrequency::Types> specsys_freq_types = {{"TOPOCENT", casacore::MFrequency::TOPO},
        {"GEOCENTR", casacore::MFrequency::GEO}, {"BARYCENT", casacore::MFrequency::BARY}, {"HELIOCEN", casacore::MFrequency::BARY},
        {"LSRK", casacore::MFrequency::LSRK}, {"LSRD", casacore::MFrequency::LSRD}, {"GALACTOC", casacore::MFrequency::GALACTO},
        {"LOCALGRP", casacore::MFrequency::LGROUP}, {"CMBDIPOL", casacore::MFrequency::CMB}, {"SOURCE", casacore::MFrequency::REST}};

    if (specsys_freq_types.count(specsys)) {
        freq_type = specsys_freq_types[specsys];

        if (specsys[0] == 'H') {
            spdlog::debug("HELIOCEN reference frame unsupported, using BARYCENT instead.");
        }
    }

    return freq_type;
}

bool CartaFitsImage::AddLinearCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& linear_axes) {
    // Create casacore::LinearCoordinate from headers and add to coord_sys.
    // Returns linear axes and whether wcssub (coordinate extraction) succeeded.

    // Initialize linear wcs structure
    int nsub(1);
    ::wcsprm wcs_linear;
    wcs_linear.flag = -1;
    int status = wcsini(1, nsub, &wcs_linear);
    if (status) {
        return false;
    }

    // Extract wcs structure for any remaining axes (not direction, spectral, or stokes)
    casacore::Block<int> axes(wcs.naxis);
    axes[0] = -(WCSSUB_LONGITUDE | WCSSUB_LATITUDE | WCSSUB_SPECTRAL | WCSSUB_STOKES);
    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_linear);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_linear);
        return false;
    }

    bool ok(true);
    if (nsub > 0) {
        // Assign linear axes
        for (int i = 0; i < nsub; i++) {
            linear_axes.push_back(axes[i] - 1); // 0-based index
        }

        // Set up wcsprm struct with wcsset
        casacore::Coordinate::set_wcs(wcs_linear);

        try {
            bool one_based(true);
            casacore::LinearCoordinate linear_coord(wcs_linear, one_based);
            coord_sys.addCoordinate(linear_coord);
        } catch (const casacore::AipsError& err) {
            // Catch so the wcs sub can be freed
            ok = false;
        }
    }

    wcsfree(&wcs_linear);
    return ok;
}

void CartaFitsImage::SetCoordSysOrder(
    casacore::CoordinateSystem& coord_sys, int naxes, std::vector<int>& special_axes, std::vector<int>& lin_axes) {
    // Reorder coordinate system with special axes first then linear axes.
    // Input special_axes are: [long, lat, spectral, stokes, linear_spectral].

    // Number of special axes set
    int nspecial(0);
    for (auto axis : special_axes) {
        if (axis >= 0) {
            ++nspecial;
        }
    }

    int long_axis(special_axes[0]), stokes_axis(special_axes[3]), linear_index(0);
    casacore::Vector<casacore::Int> order(naxes);

    for (int i = 0; i < naxes; ++i) {
        if (i == long_axis) {
            order(i) = 0; // long axis first
        } else if (i == special_axes[1]) {
            order(i) = 1; // lat axis second
        } else if (i == stokes_axis) {
            if (long_axis >= 0) {
                order(i) = 2; // stokes axis after dir axes
            } else {
                order(i) = 0; // stokes axis first if no dir axes
            }
        } else if (i == special_axes[2]) {
            if ((long_axis >= 0) && (stokes_axis >= 0)) {
                order(i) = 3; // spectral axis after dir and stokes axes
            } else if (long_axis >= 0) {
                order(i) = 2; // spectral axis after dir axes
            } else if (stokes_axis >= 0) {
                order(i) = 1; // spectral axis after stokes axis
            } else {
                order(i) = 0; // spectral axis first before linear
            }
        } else if (i == special_axes[4]) {
            order(i) = nspecial - 1; // linear spectral axis after special
        } else {
            order(i) = nspecial + linear_index; // linear after special
            ++linear_index;
        }
    }

    // Set world axes and pixel axes in same order
    coord_sys.transpose(order, order);
}

void CartaFitsImage::SetHeaderRec(char* header, casacore::RecordInterface& header_rec) {
    // Parse keyrecords into casacore Record
    int nkeys = strlen(header) / 80;
    int nkey_ids(0), nreject(0);
    ::fitskeyid key_ids[1];
    ::fitskey* fits_keys; // struct: using keyword, type, keyvalue, comment, ulen
    int status = fitshdr(header, nkeys, nkey_ids, key_ids, &nreject, &fits_keys);

    if (status) {
        spdlog::debug("Coordinate system error: wcslib FITS header parser error");
        throw(casacore::AipsError("Coordinate system setup failed."));
    }

    for (int i = 0; i < nkeys; ++i) {
        // Each keyrecord is a subrecord in the headers Record
        // name
        casacore::String name(fits_keys[i].keyword);
        name.downcase();

        if (!header_rec.isDefined(name)) {
            // name: value, comment/unit
            casacore::Record sub_record;

            // type: if type < 0, syntax error encountered
            int type = abs(fits_keys[i].type);

            switch (type) {
                case 0: // no value
                    break;
                case 1: { // logical, represented as int
                    casacore::Bool value(fits_keys[i].keyvalue.i > 0);
                    sub_record.define("value", value);
                    break;
                }
                case 2: { // int
                    casacore::Int value(fits_keys[i].keyvalue.i);
                    sub_record.define("value", value);
                    break;
                }
                case 3: { // int64
                    casacore::Int64 value(fits_keys[i].keyvalue.i);
                    sub_record.define("value", value);
                    break;
                }
                case 4: // very long int, not supported
                    break;
                case 5: { // float
                    casacore::Float value(fits_keys[i].keyvalue.i);
                    sub_record.define("value", value);
                    break;
                }
                case 6:   // integer complex
                case 7: { // double complex
                    casacore::Complex value(fits_keys[i].keyvalue.c[0], fits_keys[i].keyvalue.c[1]);
                    sub_record.define("value", value);
                    break;
                }
                case 8: { // string
                    casacore::String value(fits_keys[i].keyvalue.i);
                    sub_record.define("value", value);
                    break;
                }
                default:
                    break;
            }

            // Add unit and comment
            if (sub_record.isDefined("value")) {
                casacore::String comment(fits_keys[i].comment);
                if (fits_keys[i].ulen > 0) {
                    // comment contains units string in standard format; set substring to unit
                    casacore::String unit(comment, 1, fits_keys[i].ulen - 2);
                    sub_record.define("unit", unit);
                } else {
                    sub_record.define("comment", comment);
                }
            }

            // Add to headers Record
            header_rec.defineRecord(name, sub_record);
        }
    }

    free(fits_keys);
}

void CartaFitsImage::ReadBeamsTable(casacore::ImageInfo& image_info) {
    // Read BEAMS Binary Table to set ImageBeamSet in ImageInfo
    fitsfile* fptr;
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status) {
        throw(casacore::AipsError("Error opening FITS file."));
    }

    // Open binary table extension with name BEAMS
    int hdutype(BINARY_TBL), extver(0);
    char extname[] = "BEAMS";
    status = 0;
    fits_movnam_hdu(fptr, hdutype, extname, extver, &status);

    if (status) {
        status = 0;
        fits_close_file(fptr, &status);
        spdlog::info("Inconsistent header: could not find BEAMS table.");
        return;
    }

    // Header keywords: nrow, ncol, nchan, npol, tfields, ttype, tunit
    // Check nrow and ncol
    long nrow(0);
    int ncol(0), nchan(0), npol(0), tfields(0);

    status = 0;
    fits_get_num_rows(fptr, &nrow, &status);
    status = 0;
    fits_get_num_cols(fptr, &ncol, &status);

    if (status || (nrow * ncol == 0)) {
        status = 0;
        fits_close_file(fptr, &status);
        spdlog::info("BEAMS table is empty.");
        return;
    }

    // Check nchan and npol
    char* comment(nullptr); // ignore
    std::string key("NCHAN");
    status = 0;
    fits_read_key(fptr, TINT, key.c_str(), &nchan, comment, &status);

    key = "NPOL";
    status = 0;
    fits_read_key(fptr, TINT, key.c_str(), &npol, comment, &status);

    if (nchan * npol == 0) {
        status = 0;
        fits_close_file(fptr, &status);
        spdlog::info("BEAMS table nchan or npol is zero.");
        return;
    }

    // Get names and units for tfields; not all names have units
    key = "TFIELDS";
    status = 0;
    fits_read_key(fptr, TINT, key.c_str(), &tfields, comment, &status);

    std::unordered_map<string, string> beam_units;
    for (int i = 0; i < tfields; ++i) {
        char name[FLEN_VALUE], unit[FLEN_VALUE]; // cfitsio constant: max value length
        std::string index_str = std::to_string(i + 1);

        key = "TTYPE" + index_str;
        status = 0;
        fits_read_key(fptr, TSTRING, key.c_str(), name, comment, &status);

        key = "TUNIT" + index_str;
        status = 0;
        fits_read_key(fptr, TSTRING, key.c_str(), unit, comment, &status);

        beam_units[name] = unit;
    }

    // Read columns into vectors
    int casesen(CASEINSEN), colnum(0), anynul(0), datatype(0);
    long repeat, width;
    LONGLONG firstrow(1), firstelem(1);
    float* fnulval(nullptr);
    int* inulval(nullptr);

    std::unordered_map<std::string, std::vector<casacore::Quantity>> beam_qualities = {{"BMAJ", std::vector<casacore::Quantity>(nrow)},
        {"BMIN", std::vector<casacore::Quantity>(nrow)}, {"BPA", std::vector<casacore::Quantity>(nrow)}};

    for (auto& beam_quality : beam_qualities) {
        auto name = beam_quality.first;
        status = 0;
        fits_get_colnum(fptr, casesen, name.data(), &colnum, &status);
        fits_get_coltype(fptr, colnum, &datatype, &repeat, &width, &status);

        if (datatype == TDOUBLE) {
            std::vector<double> values(nrow);
            fits_read_col(fptr, TDOUBLE, colnum, firstrow, firstelem, nrow, fnulval, values.data(), &anynul, &status);
            for (int i = 0; i < nrow; ++i) {
                beam_quality.second[i] = casacore::Quantity(values[i], beam_units[name]);
            }
        } else {
            std::vector<float> values(nrow);
            fits_read_col(fptr, TFLOAT, colnum, firstrow, firstelem, nrow, fnulval, values.data(), &anynul, &status);
            for (int i = 0; i < nrow; ++i) {
                beam_quality.second[i] = casacore::Quantity(values[i], beam_units[name]);
            }
        }
    }

    std::unordered_map<std::string, std::vector<int>> beam_indices = {{"CHAN", std::vector<int>(nrow)}, {"POL", std::vector<int>(nrow)}};

    for (auto& beam_index : beam_indices) {
        auto name = beam_index.first;
        status = 0;
        fits_get_colnum(fptr, casesen, name.data(), &colnum, &status);
        fits_read_col(fptr, TINT, colnum, firstrow, firstelem, nrow, inulval, beam_index.second.data(), &anynul, &status);
    }

    fits_close_file(fptr, &status);

    image_info.setAllBeams(nchan, npol, casacore::GaussianBeam::NULL_BEAM);
    for (int i = 0; i < nrow; ++i) {
        casacore::GaussianBeam beam(beam_qualities["BMAJ"][i], beam_qualities["BMIN"][i], beam_qualities["BPA"][i]);
        image_info.setBeam(beam_indices["CHAN"][i], beam_indices["POL"][i], beam);
    }
}

void CartaFitsImage::AddObsInfo(casacore::CoordinateSystem& coord_sys, casacore::RecordInterface& header_rec) {
    // Add ObsInfo (observer, telescope, date) to coordinate system, and update header_rec
    casacore::Vector<casacore::String> error;
    casacore::ObsInfo obs_info;
    obs_info.fromFITS(error, header_rec);
    coord_sys.setObsInfo(obs_info);

    // Remove used ObsInfo keys from header_rec
    casacore::Vector<casacore::String> obs_keys = casacore::ObsInfo::keywordNamesFITS();
    for (auto& obs_key : obs_keys) {
        if (header_rec.isDefined(obs_key)) {
            header_rec.removeField(obs_key);
        }
    }
}

void CartaFitsImage::SetPixelMask() {
    // Set pixel mask for entire image
    fitsfile* fptr = OpenFile();

    // Read blanked mask from image
    bool ok(false);
    casacore::ArrayLattice<bool> mask_lattice;
    switch (_bitpix) {
        case 8: {
            ok = GetPixelMask<unsigned char>(fptr, _bitpix, _shape, mask_lattice);
            break;
        }
        case 16: {
            ok = GetPixelMask<short>(fptr, _bitpix, _shape, mask_lattice);
            break;
        }
        case 32: {
            ok = GetPixelMask<int>(fptr, _bitpix, _shape, mask_lattice);
            break;
        }
        case 64: {
            ok = GetPixelMask<LONGLONG>(fptr, _bitpix, _shape, mask_lattice);
            break;
        }
        case -32: {
            ok = GetNanPixelMask<float>(mask_lattice);
            break;
        }
        case -64: {
            ok = GetNanPixelMask<double>(mask_lattice);
            break;
        }
        default: {
            ok = false;
            break;
        }
    }

    if (!ok) {
        spdlog::error("FITS read pixel mask failed.");
        _pixel_mask = nullptr;
    } else {
        _pixel_mask = new casacore::ArrayLattice<bool>(mask_lattice);
    }

    CloseFile();
}

bool CartaFitsImage::doGetNanMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    // Create mask from finite (not NaN or infinite) values in slice
    casacore::Array<float> data;
    if (doGetSlice(data, section)) {
        buffer = isFinite(data);
        return true;
    }

    return false;
}
