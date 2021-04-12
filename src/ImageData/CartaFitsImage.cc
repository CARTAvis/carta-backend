/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaFitsImage.cc : specialized casacore::ImageInterface implementation for FITS

#include "CartaFitsImage.h"

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/FITSCoordinateUtil.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/Stokes.h>

#include <wcslib/fitshdr.h>
#include <wcslib/wcs.h>
#include <wcslib/wcsfix.h>
#include <wcslib/wcshdr.h>
#include <wcslib/wcsmath.h>
/*
#include <wcslib/wcsconfig.h>
*/

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
    throw(casacore::AipsError("CartaFitsImage doGetSlice not implemented."));

    /*
    fitsfile* fptr = OpenFile();

    switch (_datatype) {
        case 8:
            break;
        case 16:
            break;
        case 32:
            break;
        case 64:
            break;
        case -32:
            break;
        case -64:
            break;
    }

   Array<Double> tmp;
   pTiledFile_p->get (tmp, section);
   buffer.resize(tmp.shape());
   convertArray(buffer, tmp);

   pTiledFile_p->get (buffer, section, scale_p, offset_p, longMagic_p, hasBlanks_p);
    */

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
    // TODO
    // return shape_p.tileShape().product();
    throw(casacore::AipsError("CartaFitsImage advisedMaxPixels not implemented."));
}

casacore::IPosition CartaFitsImage::doNiceCursorShape(casacore::uInt maxPixels) const {
    // TODO
    // return shape_p.tileShape();
    throw(casacore::AipsError("CartaFitsImage doNiceCursorShape not implemented."));
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

    throw(casacore::AipsError("CartaFitsImage pixelMask not implemented."));
    return *_pixel_mask;
}

casacore::Lattice<bool>& CartaFitsImage::pixelMask() {
    if (!_has_blanks) {
        throw(casacore::AipsError("CartaFitsImage::pixelMask - no pixel mask used"));
    }

    throw(casacore::AipsError("CartaFitsImage pixelMask not implemented."));
    return *_pixel_mask;
}

casacore::Bool CartaFitsImage::doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) {
    if (!_has_blanks) {
        buffer.resize(section.length());
        buffer = true;
        return false;
    }

    throw(casacore::AipsError("CartaFitsImage doGetMaskSlice not implemented."));
    return _pixel_mask->getSlice(buffer, section);
}

// private

fitsfile* CartaFitsImage::OpenFile() {
    // Open file and return file pointer
    fitsfile* fptr;
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status) {
        throw(casacore::AipsError("Error opening FITS file."));
    }

    return fptr;
}

void CartaFitsImage::CloseFile(fitsfile* fptr) {
    int status = 0;
    fits_close_file(fptr, &status);
}

void CartaFitsImage::CloseFileIfError(fitsfile* fptr, const int& status, const std::string& error) {
    // Close file if cfitsio status is not 0 (ok).  If error, throw exception.
    if (status) {
        CloseFile(fptr);

        if (!error.empty()) {
            throw(casacore::AipsError(error));
        }
    }
}

void CartaFitsImage::SetUpImage() {
    // Set up image parameters and coordinate system from headers
    // Get headers as single string
    int nheaders(0);
    std::string header_str;
    GetFitsHeaders(nheaders, header_str);

    // Convert headers to Vector of Strings to pass to casacore converter
    casacore::Vector<casacore::String> header_strings(nheaders);
    size_t pos(0);
    for (int i = 0; i < nheaders; ++i) {
        header_strings(i) = header_str.substr(pos, 80);
        pos += 80;
    }

    casacore::Record unused_headers;
    casacore::LogSink sink;
    casacore::LogIO log(sink);
    casacore::CoordinateSystem coord_sys;
    int stokes_fits_value(1);
    bool drop_stokes(true);

    try {
        // Set coordinate system
        coord_sys = casacore::ImageFITSConverter::getCoordinateSystem(
            stokes_fits_value, unused_headers, header_strings, log, 0, _shape, drop_stokes);
    } catch (const casacore::AipsError& err) {
        if (err.getMesg().startsWith("Tabular Coordinate")) {
            // Spectral axis defined in velocity fails if no rest freq to convert to frequencies
            try {
                // Set up with wcslib
                coord_sys = SetUpCoordinateSystem(nheaders, header_str, unused_headers);
            } catch (const casacore::AipsError& err) {
                spdlog::debug("Coordinate system setup error: {}", err.getMesg());
                throw(casacore::AipsError("Coordinate system setup from FITS headers failed."));
            }
        } else {
            spdlog::debug("Coordinate system setup error: {}", err.getMesg());
            throw(casacore::AipsError("Coordinate system setup from FITS headers failed."));
        }
    }

    // Set coord sys in image
    setCoordinateInfo(coord_sys);

    try {
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

void CartaFitsImage::GetFitsHeaders(int& nkeys, std::string& hdrstr) {
    // Read header values into single string, and store some image parameters.
    // Returns string and number of keys contained in string.
    // Throws exception if any headers missing.

    fitsfile* fptr = OpenFile();

    // Advance to requested hdu
    int hdu(_hdu + 1);
    int* hdutype(nullptr);
    int status(0);
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
    }

    // Get headers to set up image:
    // Previous functions converted compressed headers automatically; must call specific function for uncompressed headers
    // Determine whether tile compressed
    status = 0;
    _is_compressed = fits_is_compressed_image(fptr, &status);
    CloseFileIfError(fptr, status, "Error detecting image compression.");

    // Number of headers (keys)
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

    hdrstr = std::string(header[0]);

    // Free memory allocated by cfitsio
    int free_status(0);
    fits_free_memory(*header, &free_status);

    // Done with file
    CloseFile(fptr);
}

casacore::CoordinateSystem CartaFitsImage::SetUpCoordinateSystem(
    int nkeys, const std::string& header_str, casacore::RecordInterface& unused_headers) {
    // Sets up coordinate sys with wcslib due to error thrown by casacore, or throws exception.
    // Based on casacore::FITSCoordinateUtil::fromFITSHeader.
    //   Unfortunately, only this top-level function is public,
    //   so if any coordinate "fails" the entire coordinate system fails.
    //   It is impossible to reuse the code for the "good" coordinates.
    casacore::CoordinateSystem coord_sys;

    // Parse header string into wcsprm struct; removes used keyrecords
    // inputs:
    char* header;
    std::strcpy(header, header_str.c_str());
    int relax(WCSHDR_all); // allow informal extensions of WCS standard
    int ctrl(-2);          // report rejected keys and remove
    // outputs:
    int nreject(0); // number of rejected keys
    int nwcs(0);    // number of coordinate representations found
    ::wcsprm* wcs_ptr(nullptr);

    int status = wcspih(header, nkeys, relax, ctrl, &nreject, &nwcs, &wcs_ptr);
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

    // Determine the coordinates (longitude, latitude, spectral, stokes axes)
    int long_axis(-1), lat_axis(-1), spec_axis(-1), stokes_axis(-1);

    const ::wcsprm wcs0 = wcs_ptr[0];

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
    ok = AddStokesCoordinate(coord_sys, wcs0, _shape, stokes_axis);
    if (!ok) {
        wcsvfree(&nwcs, &wcs_ptr);
        throw(casacore::AipsError("Coordinate system setup failed."));
    }

    // Put unused keyrecords in header string into Record
    casacore::Record header_rec;
    SetUnusedHeaderRec(header, header_rec);

    casacore::UnitMap::addFITS(); // add FITS units for Quanta

    AddObsInfo(coord_sys, header_rec);

    return coord_sys;
}

bool CartaFitsImage::AddDirectionCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& direction_axes) {
    // Create casacore::DirectionCoordinate from headers and add to coord_sys.
    // Returns index of direction axes in vector and whether wcssub (coordinate extraction) succeeded.

    // Extract LAT/LONG wcs structure
    int nsub(2);
    casacore::Block<int> axes(nsub);
    axes[0] = WCSSUB_LONGITUDE;
    axes[1] = WCSSUB_LATITUDE;
    ::wcsprm wcs_long_lat;
    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_long_lat);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_long_lat);
        spdlog::debug("Failed to extract wcs latitude/longitude coordinates.");
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
            casacore::DirectionCoordinate direction_coord(direction_type, wcs_long_lat, true);
            coord_sys.addCoordinate(direction_coord);
        } else {
            // If nsub is 2, should have been able to determine direction type
            ok = false;
        }
    }

    wcsfree(&wcs_long_lat);
    return ok;
}

bool CartaFitsImage::AddStokesCoordinate(
    casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, const casacore::IPosition& shape, int& stokes_axis) {
    // Create casacore::StokesCoordinate from headers and add to coord_sys.
    // Returns stokes axis and whether wcssub (coordinate extraction) succeeded.

    // Extract STOKES wcs structure
    int nsub(1);
    casacore::Block<int> axes(nsub);
    axes[0] = WCSSUB_STOKES;
    ::wcsprm wcs_stokes;
    try {
        casacore::Coordinate::sub_wcs(wcs, nsub, axes.storage(), wcs_stokes);
    } catch (const casacore::AipsError& err) {
        spdlog::debug(err.getMesg());
        wcsfree(&wcs_stokes);
        spdlog::debug("Failed to extract wcs stokes coordinate.");
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
                return false;
            }
        }

        // Set up wcsprm struct with wcsset
        casacore::Coordinate::set_wcs(wcs_stokes);

        // Get FITS header values CRPIX, CRVAL, CDELT
        double crpix = wcs_stokes.crpix[0] - 1.0; // 0-based
        double crval = wcs_stokes.crval[0];
        double cdelt = wcs_stokes.cdelt[0];

        // Determine stokes type for each index
        casacore::Vector<casacore::Int> stokes_types(stokes_length);

        for (size_t i = 0; i < stokes_length; ++i) {
            double tmp = crval + (i - crpix) * cdelt;
            int tmp_stokes = (tmp >= 0 ? int(tmp + 0.01) : int(tmp - 0.01));

            switch (tmp_stokes) {
                case 0: {
                    spdlog::debug("Detected Stokes coordinate = 0, setting to Undefined.");
                    stokes_types(i) = casacore::Stokes::Undefined;
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
                    break;
                }
                case 9: {
                    spdlog::debug("Detected Stokes coordinate is unofficial optical depth, setting to Undefined.");
                    stokes_types(i) = casacore::Stokes::Undefined;
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

        casacore::StokesCoordinate stokes_coord(stokes_types);
        coord_sys.addCoordinate(stokes_coord);
    }

    wcsfree(&wcs_stokes);
    return ok;
}

void CartaFitsImage::SetUnusedHeaderRec(char* header, casacore::RecordInterface& unused_headers) {
    // Parse unused keyrecords into casacore Record
    int nkeys = strlen(header) / 80; // update for unused keys
    int nkey_ids(0), nreject(0);
    ::fitskeyid key_ids[1];
    ::fitskey* fits_keys; // struct: using keyword, type, keyvalue, comment, ulen
    int status = fitshdr(header, nkeys, nkey_ids, key_ids, &nreject, &fits_keys);

    if (status) {
        spdlog::debug("Coordinate system error: wcslib FITS header parser error");
        throw(casacore::AipsError("Coordinate system setup failed."));
    }

    for (int i = 0; i < nkeys; ++i) {
        // Each keyrecord is a subrecord in the unused_headers Record
        // name: value, comment/unit
        casacore::Record sub_record;

        // name
        casacore::String name(fits_keys[i].keyword);
        name.downcase();

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

        // Add to unused_headers Record unless already defined
        if (!unused_headers.isDefined(name)) {
            unused_headers.defineRecord(name, sub_record);
        }
    }

    free(fits_keys);
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
