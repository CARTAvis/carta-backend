/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FitsLoader.h"

#include <fitsio.h>
#include <regex>

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/images/Images/FITSImage.h>

#include "CartaFitsImage.h"
#include "CompressedFits.h"
#include "Util/Casacore.h"
#include "Util/FileSystem.h"

namespace carta {

FitsLoader::FitsLoader(const std::string& filename, bool is_gz, bool is_http)
    : FileLoader(filename, "", is_gz, is_http), _is_http(is_http) {}

FitsLoader::~FitsLoader() {
    // Remove decompressed fits.gz file
    auto unzip_path = fs::path(_unzip_file);
    std::error_code error_code;
    if (fs::exists(unzip_path, error_code)) {
        fs::remove(unzip_path);
    }
}

void FitsLoader::AllocateImage(const std::string& hdu) {
    // Open image file as a casacore::ImageInterface<float>

    // Convert string to FITS hdu number
    casacore::uInt hdu_num(_is_http ? 0 : FileInfo::GetFitsHdu(hdu));

    if (!_image || (hdu_num != _hdu_num)) {
        bool gz_mem_ok(true);

        if (_is_gz) {
            // Determine whether to load into memory or decompress on disk.
            // Do not check memory if for headers only.
            // Error if failure reading naxis/bitpix headers.
            CompressedFits fits_gz(_filename);
            auto required_mem_kB = fits_gz.GetDecompressSize();

            if (required_mem_kB == 0) {
                throw(casacore::AipsError("Error reading FITS gz file."));
            }

            auto free_mem_kB = casacore::HostInfo::memoryFree();
            gz_mem_ok = (free_mem_kB > required_mem_kB);
            spdlog::debug("required mem={} kB, free mem={} kB, access file in memory={}", required_mem_kB, free_mem_kB, gz_mem_ok);

            if (!gz_mem_ok) {
                std::string error;
                if (!fits_gz.DecompressGzFile(_unzip_file, error)) {
                    throw(casacore::AipsError("Decompress FITS gz file failed: " + error));
                }
            }
        }

        // Default is casacore::FITSImage; if fails, try CartaFitsImage
        bool use_casacore_fits(true);

        if (_is_http) {
            use_casacore_fits = false;
        } else {
            std::string error;
            auto num_headers = GetNumImageHeaders(_filename, hdu_num, error);

            if (num_headers == 0) {
                throw(casacore::AipsError(error));
            }

            if (num_headers > 2000) {
                // casacore::FITSImage parses HISTORY
                use_casacore_fits = false;
            }
        }

        try {
            if (_is_gz) {
                if (gz_mem_ok) {
                    // Use cfitsio to access data in CartaFitsImage.
                    // casacore throws exception "No data in the zeroth or first extension"
                    use_casacore_fits = false;
                    _image.reset(new CartaFitsImage(_filename, hdu_num));
                } else {
                    // use casacore for unzipped FITS file
                    _image.reset(new casacore::FITSImage(_unzip_file, 0, hdu_num));
                }
            } else if (_is_http) {
                _image.reset(new CartaFitsImage(_filename, hdu_num, true));
            } else if (use_casacore_fits) {
                if (Is64BitBeamsTable(_filename)) {
                    use_casacore_fits = false;
                    _image.reset(new CartaFitsImage(_filename, hdu_num));
                } else {
                    _image.reset(new casacore::FITSImage(_filename, 0, hdu_num));
                }
            } else {
                _image.reset(new CartaFitsImage(_filename, hdu_num));
            }
        } catch (const casacore::AipsError& err) {
            if (use_casacore_fits) {
                // casacore::FITSImage failed, try CartaFitsImage
                try {
                    use_casacore_fits = false;
                    _image.reset(new CartaFitsImage(_filename, hdu_num));
                } catch (const casacore::AipsError& err) {
                    spdlog::error(err.getMesg());
                }
            } else {
                auto error = err.getMesg();
                spdlog::error(error);
                throw(casacore::AipsError(error));
            }
        }

        if (!_image) {
            throw(casacore::AipsError("Error loading FITS image."));
        }

        // Remove restoring beam if not in headers unless AIPS beam support enabled
        ResetImageBeam(hdu_num);

        _hdu = hdu;
        _hdu_num = hdu_num;

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));

        if (use_casacore_fits) {
            casacore::FITSImage* fits_image = dynamic_cast<casacore::FITSImage*>(_image.get());
            _data_type = fits_image->internalDataType();
        } else {
            CartaFitsImage* fits_image = dynamic_cast<CartaFitsImage*>(_image.get());
            _data_type = fits_image->internalDataType();
        }
    }
}

int FitsLoader::GetNumImageHeaders(const std::string& filename, int hdu, std::string& error) {
    // Return number of FITS headers if image hdu, 0 if error.
    int num_headers(0);

    // Open file read-only
    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, filename.c_str(), 0, &status);
    if (status) {
        error = "Error reading FITS file.";
        return num_headers;
    }

    // Advance to hdu (FITS hdu is 1-based) and check if image
    int hdutype(-1);
    fits_movabs_hdu(fptr, hdu + 1, &hdutype, &status);
    if (status) {
        error = "Cannot advance to requested HDU.";
        return num_headers;
    }
    if (hdutype != IMAGE_HDU) {
        error = "HDU is not an image.";
        return num_headers;
    }

    // Check if image exists in HDU
    std::string key("NAXIS");
    char* comment(nullptr); // unused
    int naxis(0);
    fits_read_key(fptr, TINT, key.c_str(), &naxis, comment, &status);
    if (naxis == 0) {
        error = "HDU image is empty.";
        return num_headers;
    }

    fits_get_hdrspace(fptr, &num_headers, nullptr, &status);
    fits_close_file(fptr, &status);

    return num_headers;
}

void FitsLoader::ResetImageBeam(unsigned int hdu_num) {
    // Remove restoring beam not if not in header entries.
    // If supporting AIPS beam, use last beam from history instead.
    // Remote files can't have restoring beams
    if (!_image || _is_http) {
        return;
    }

    bool has_beam_headers = HasBeamHeaders(hdu_num); // BMAJ, BMIN, and BPA

    if (!has_beam_headers) {
        auto image_info = _image->imageInfo();

        if (image_info.hasBeam() && image_info.getBeamSet().hasSingleBeam()) {
            // Remove beam set by casacore (first history beam)
            image_info.removeRestoringBeam();
            _image->setImageInfo(image_info);
        }

        if (_support_aips_beam) {
            // Set beam from last history header instead
            casacore::Quantity major, minor, pa;

            if (GetLastHistoryBeam(hdu_num, major, minor, pa)) {
                image_info.setRestoringBeam(major, minor, pa);
                _image->setImageInfo(image_info);
                _is_history_beam = true;
            }
        }
    }
}

bool FitsLoader::HasBeamHeaders(unsigned int hdu_num) {
    // Check headers for BMAJ, BMIN, BPA keywords
    fitsfile* fptr;
    int status(0), hdu(hdu_num + 1); // 1-based for FITS
    int* hdutype(nullptr);

    // Open file and move to hdu
    fits_open_file(&fptr, _filename.c_str(), 0, &status);
    fits_movabs_hdu(fptr, hdu, hdutype, &status);

    // Read headers
    std::string record(80, 0);
    int key_num(1);
    bool bmaj_found(false), bmin_found(false), bpa_found(false);
    bool found_beam(false);

    while (status == 0 && !found_beam) {
        fits_read_record(fptr, key_num++, record.data(), &status);

        if (status == 0) {
            std::string keyword = record.substr(0, 4);
            bmaj_found |= (keyword == "BMAJ");
            bmin_found |= (keyword == "BMIN");
            bpa_found |= (keyword == "BPA ");
        }

        found_beam = bmaj_found && bmin_found && bpa_found;
    }

    // Close file
    status = 0;
    fits_close_file(fptr, &status);

    return found_beam;
}

bool FitsLoader::GetLastHistoryBeam(unsigned int hdu_num, casacore::Quantity& major, casacore::Quantity& minor, casacore::Quantity& pa) {
    // Check HISTORY headers for BMAJ, BMIN, BPA, Beam keywords
    // Check headers for BMAJ, BMIN, BPA keywords
    fitsfile* fptr;
    int status(0), hdu(hdu_num + 1); // 1-based for FITS
    int* hdutype(nullptr);

    // Open file and move to hdu
    fits_open_file(&fptr, _filename.c_str(), 0, &status);
    fits_movabs_hdu(fptr, hdu, hdutype, &status);

    // Read headers in order, keep the last one
    int key_num(1);
    bool found_history_beam(false);

    while (status == 0) {
        std::string record(80, 0);
        fits_read_record(fptr, key_num++, record.data(), &status);

        if ((status == 0) && (record.substr(0, 7) == "HISTORY") &&
            ((record.find("Beam") != std::string::npos) || (record.find("BMAJ") != std::string::npos))) {
            std::string bmaj, bmin, bpa;
            if (ParseHistoryBeamHeader(record, bmaj, bmin, bpa)) {
                try {
                    // Set return values if readQuantity succeeds
                    casacore::Quantity qmajor, qminor, qpa;
                    casacore::readQuantity(qmajor, bmaj);
                    casacore::readQuantity(qminor, bmin);
                    casacore::readQuantity(qpa, bpa);

                    major = qmajor;
                    minor = qminor;
                    pa = qpa;
                    found_history_beam = true;
                } catch (const casacore::AipsError& err) {
                    spdlog::debug("Unable to set history header beam {}, unexpected format.", record);
                }
            }
        }
    }

    return found_history_beam;
}

bool FitsLoader::Is64BitBeamsTable(const std::string& filename) {
    fitsfile* fptr;
    int status(0);
    fits_open_file(&fptr, filename.c_str(), 0, &status);

    if (status) {
        spdlog::error("Error opening FITS file.");
        return false;
    }

    // Open binary table extension with name BEAMS
    int hdutype(BINARY_TBL), extver(0);
    std::string extname("BEAMS");
    status = 0;
    fits_movnam_hdu(fptr, hdutype, extname.data(), extver, &status);

    if (status) {
        status = 0;
        fits_close_file(fptr, &status);
        spdlog::info("Could not find BEAMS table.");
        return false;
    }

    // Get BEAMS columns data type
    int casesen(CASEINSEN), colnum(0), typecode(0);
    long repeat, width;
    std::vector<std::string> keys = {"BMAJ", "BMIN", "BPA", "CHAN", "POL"};
    for (auto& key : keys) {
        status = 0;
        fits_get_colnum(fptr, casesen, key.data(), &colnum, &status);
        fits_get_coltype(fptr, colnum, &typecode, &repeat, &width, &status);
        if (typecode == TDOUBLE) {
            fits_close_file(fptr, &status);
            spdlog::warn("BEAMS table consists of 64-bit parameters.");
            return true;
        }
    }

    fits_close_file(fptr, &status);
    return false;
}

} // namespace carta
