/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// FitsHduList.cc: Fill HDU list vector with hdu number and extension name

#include "FitsHduList.h"

#include "../Logger/Logger.h"

FitsHduList::FitsHduList(const std::string& filename) {
    _filename = filename;
}

void FitsHduList::GetHduList(std::vector<std::string>& hdu_list, std::string& error) {
    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status) {
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
    int status(0);

    int hdunum(0);
    fits_get_num_hdus(fptr, &hdunum, &status);

    for (int hdu = 0; hdu < hdunum; ++hdu) {
        // Add to hdulist if naxis > 0 and primary array or image extension
        // Common arguments for fits_read_key
        char comment[70];

        bool is_image(false), is_fz(false);
        std::string key;

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
            // Check XTENSION; must be "IMAGE"
            status = 0;
            key = "XTENSION";
            char xtension[70];
            fits_read_key(fptr, TSTRING, key.c_str(), xtension, comment, &status);

            if (!status) {
                std::string ext_type(xtension);
                is_image = (ext_type.find("IMAGE") != std::string::npos);
            }

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

        if ((hdu > 0) && !is_image) {
            // Move to next hdu if not image extension
            int* hdutype(nullptr);
            fits_movrel_hdu(fptr, 1, hdutype, &status);
            continue;
        }

        // Check NAXIS for primary hdu or image extension
        status = 0;
        key = (is_fz ? "ZNAXIS" : "NAXIS");
        int naxis(0);
        fits_read_key(fptr, TINT, key.c_str(), &naxis, comment, &status);

        if (!status && (naxis > 1)) {
            // Check BITPIX
            status = 0;
            key = (is_fz ? "ZBITPIX" : "BITPIX");
            int bitpix(0);
            fits_read_key(fptr, TINT, key.c_str(), &bitpix, comment, &status);

            bool bitpix_valid(false);
            if (!status) {
                std::vector<int> valid_bitpix = {8, 16, 32, 64, -32, -64};

                for (auto value : valid_bitpix) {
                    if (value == bitpix) {
                        bitpix_valid = true;
                        break;
                    }
                }
            }

            if (bitpix_valid) {
                // Add to hdu list
                std::string hdu_name = std::to_string(hdu);

                if (hdu == 0) {
                    // Add primary data array
                    hdu_list.push_back(hdu_name);
                } else {
                    // Get extension name
                    status = 0;
                    key = "EXTNAME";
                    char extname[70];
                    fits_read_key(fptr, TSTRING, key.c_str(), extname, comment, &status);

                    if (!status) {
                        // Add hdu with ext name
                        std::string ext_name(extname);
                        std::string hdu_ext_name = fmt::format("{}: {}", hdu_name, ext_name);
                        hdu_list.push_back(hdu_ext_name);
                    } else {
                        // Add hdu without ext name
                        hdu_list.push_back(hdu_name);
                    }
                }
            } else {
                spdlog::debug("FITS HDU {} invalid BITPIX", hdu);
            }
        }

        // Move to next hdu
        int* hdutype(nullptr);
        fits_movrel_hdu(fptr, 1, hdutype, &status);
    }
}
