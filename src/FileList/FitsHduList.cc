/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// FitsHduList.cc: Fill HDU list vector with hdu number and extension name

#include "FitsHduList.h"

#include <fitsio.h>

#include <casacore/casa/Exceptions/Error.h>

#include "Logger/Logger.h"
#include "Util/File.h"

using namespace carta;

FitsHduList::FitsHduList(const std::string& filename) {
    _filename = filename;
}

void FitsHduList::GetHduList(std::vector<std::string>& hdu_list, std::string& error) {
    // Returns list of hdu num and ext name for primary array and image extensions.

    // DO NOT USE for compressed FITS, fits_open_file decompresses entire file.
    if (IsCompressedFits(_filename)) {
        error = "Should use CompressedFits for HDU header map.";
        spdlog::debug(error);
        return;
    }

    // Open file read-only
    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status) {
        spdlog::debug("FITS {} fits_open_file status {}", _filename, status);
        error = "FITS open file error.";
        return;
    }

    CheckFitsHeaders(fptr, hdu_list, error);
    fits_close_file(fptr, &status);

    if (hdu_list.empty() && error.empty()) {
        error = "No images found in FITS file.";
    }
}

void FitsHduList::CheckFitsHeaders(fitsfile* fptr, std::vector<std::string>& hdu_list, std::string& error) {
    // Use cfitsio lib to check headers for hdu list
    int status(0); // status must be equal to 0 on input, else functions immediately exit

    int hdunum(0);
    fits_get_num_hdus(fptr, &hdunum, &status);

    for (int hdu = 0; hdu < hdunum; ++hdu) {
        // Add to hdulist if primary array or image extension with naxis > 0.

        // Common arguments for fits_read_key
        std::string key;
        char* comment(nullptr); // unused

        bool is_image(false), is_fz(false);

        if (hdu == 0) {
            // Check SIMPLE for primary header only; exit if false
            status = 0;
            key = "SIMPLE";
            bool simple(false);
            fits_read_key(fptr, TLOGICAL, key.c_str(), &simple, comment, &status);

            if (status || !simple) {
                error = "FITS error: SIMPLE missing or false.";
                break;
            }
        } else {
            int hdutype(-1);
            status = 0;
            fits_get_hdu_type(fptr, &hdutype, &status);
            is_image = (hdutype == IMAGE_HDU);

            if (!is_image) {
                // Check ZIMAGE for fz compressed image
                status = 0;
                key = "ZIMAGE";
                fits_read_key(fptr, TLOGICAL, key.c_str(), &is_fz, comment, &status);

                if (is_fz) {
                    is_image = true;
                }
            }
        }

        if ((hdu == 0) || is_image) {
            // Check image dimensions for primary hdu or image extension
            key = (is_fz ? "ZNAXIS" : "NAXIS");
            int naxis(0);
            status = 0;
            fits_read_key(fptr, TINT, key.c_str(), &naxis, comment, &status);

            if (naxis > 1) {
                // Add to hdu list
                std::string hdu_name = std::to_string(hdu);

                if (hdu == 0) {
                    // Add primary data array
                    hdu_list.push_back(hdu_name);
                } else {
                    // Get extension name
                    key = "EXTNAME";
                    char extname[FLEN_VALUE]; // cfitsio constant: max value length
                    status = 0;
                    fits_read_key(fptr, TSTRING, key.c_str(), extname, comment, &status);

                    std::string ext_name(extname);
                    if (!ext_name.empty()) {
                        // Add hdu with ext name
                        std::string hdu_ext_name = fmt::format("{}: {}", hdu_name, ext_name);
                        hdu_list.push_back(hdu_ext_name);
                    } else {
                        // Add hdu without ext name
                        hdu_list.push_back(hdu_name);
                    }
                }
            }
        }

        // Move to next hdu
        int* hdutype(nullptr);
        status = 0;
        fits_movrel_hdu(fptr, 1, hdutype, &status);
    }
}
