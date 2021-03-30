/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// FitsHduList.cc: Fill FileInfo HDU_list with hdu number and extension name

#include "FitsHduList.h"

#include <casacore/fits/FITS/fits.h>
#include <casacore/fits/FITS/fitsio.h>

FitsHduList::FitsHduList(const std::string& filename) {
    _filename = filename;
}

void FitsHduList::GetHduList(std::vector<std::string>& hdu_list) {
    casacore::FitsInput fits_input(_filename.c_str(), casacore::FITS::Disk, 10, FitsInfoErrHandler);

    auto input_error = fits_input.err();
    if (input_error == casacore::FitsIO::OK) {
        // Use casacore, iterate through each header data unit
        for (int hdu = 0; hdu < fits_input.getnumhdu(); ++hdu) {
            if (fits_input.rectype() == casacore::FITS::HDURecord) {
                std::string hdu_name = std::to_string(hdu);
                casacore::FITS::HDUType hdu_type = fits_input.hdutype();

                if (IsImageHdu(hdu_type)) {
                    // add hdu to list
                    int ndim(0);
                    std::string ext_name;
                    GetFitsHduInfo(fits_input, ndim, ext_name);
                    if (ndim > 0) {
                        if (!ext_name.empty()) {
                            hdu_name += ":" + ext_name;
                        }
                        hdu_list.push_back(hdu_name);
                    }
                    fits_input.skip_all(hdu_type); // skip data to next hdu
                } else if (hdu_type == casacore::FITS::BinaryTableHDU) {
                    // casacore::BinaryTableExtension destructor gets seg fault, use parent extension class.
                    // This generates error "Input does not contain an HDU of this type", ignored by FitsInfoErrHandler.
                    casacore::ExtensionHeaderDataUnit extension(fits_input, FitsInfoErrHandler);
                    casacore::FitsKeywordList fkw_list;

                    if (!extension.get_hdr(hdu_type, fkw_list)) {
                        // Check for FITS compressed fz: header ZIMAGE = T.
                        casacore::FitsKeyword* fkw = fkw_list("ZIMAGE");
                        if (fkw && fkw->asBool()) {
                            // Check ndim for uncompressed image
                            fkw = fkw_list("ZNAXIS");
                            if (fkw && (fkw->asInt() > 0)) {
                                std::string ext_name = extension.extname();
                                if (!ext_name.empty()) {
                                    hdu_name += ":" + ext_name;
                                }
                                hdu_list.push_back(hdu_name);
                            }
                        }
                    }
                    fits_input.skip_all(hdu_type); // skip data to next hdu
                } else {
                    fits_input.skip_hdu();
                }
            }
        }
    } else if (input_error == casacore::FitsIO::BADBEGIN) {
        // casacore FITS code does not support BITPIX = 64, use cfitsio to check headers
        fitsfile* fptr = fits_input.getfptr();
        CheckFitsHeaders(fptr, hdu_list);
    } else {
        // Log error
        switch (input_error) {
            case casacore::FitsIO::EMPTYFILE:
                FitsInfoErrHandler("FITS file is empty", casacore::FITSError::SEVERE);
                break;
            case casacore::FitsIO::IOERR:
                FitsInfoErrHandler("FITS read error", casacore::FITSError::SEVERE);
                break;
            case casacore::FitsIO::NOPRIMARY:
                FitsInfoErrHandler("FITS missing primary HDU", casacore::FITSError::SEVERE);
                break;
            default:
                break;
        }
    }
}

bool FitsHduList::IsImageHdu(casacore::FITS::HDUType hdu_type) {
    return ((hdu_type == casacore::FITS::PrimaryArrayHDU) || (hdu_type == casacore::FITS::PrimaryGroupHDU) ||
            (hdu_type == casacore::FITS::PrimaryTableHDU) || (hdu_type == casacore::FITS::ImageExtensionHDU));
}

void FitsHduList::GetFitsHduInfo(casacore::FitsInput& fits_input, int& ndim, std::string& ext_name) {
    // Return dims and extname from header
    switch (fits_input.hdutype()) {
        case casacore::FITS::PrimaryArrayHDU:
        case casacore::FITS::PrimaryGroupHDU:
        case casacore::FITS::PrimaryTableHDU: {
            casacore::HeaderDataUnit* header_unit(nullptr);
            switch (fits_input.datatype()) {
                case casacore::FITS::BYTE:
                case casacore::FITS::CHAR:
                    header_unit = new casacore::PrimaryArray<unsigned char>(fits_input);
                    break;
                case casacore::FITS::SHORT:
                    header_unit = new casacore::PrimaryArray<short>(fits_input);
                    break;
                case casacore::FITS::LONG:
                    header_unit = new casacore::PrimaryArray<int>(fits_input);
                    break;
                case casacore::FITS::FLOAT:
                    header_unit = new casacore::PrimaryArray<float>(fits_input);
                    break;
                case casacore::FITS::DOUBLE:
                    header_unit = new casacore::PrimaryArray<double>(fits_input);
                    break;
                default:
                    break;
            }
            if (header_unit != nullptr) {
                ndim = header_unit->dims();
                delete header_unit;
            }
        } break;
        case casacore::FITS::ImageExtensionHDU: {
            switch (fits_input.datatype()) {
                case casacore::FITS::BYTE:
                case casacore::FITS::CHAR: {
                    casacore::ImageExtension<unsigned char> header_unit = casacore::ImageExtension<unsigned char>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                case casacore::FITS::SHORT: {
                    casacore::ImageExtension<short> header_unit = casacore::ImageExtension<short>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                case casacore::FITS::LONG: {
                    casacore::ImageExtension<int> header_unit = casacore::ImageExtension<int>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                case casacore::FITS::FLOAT: {
                    casacore::ImageExtension<float> header_unit = casacore::ImageExtension<float>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                case casacore::FITS::DOUBLE: {
                    casacore::ImageExtension<double> header_unit = casacore::ImageExtension<double>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                default:
                    break;
            }
        } break;
        default:
            break;
    }
}

void FitsHduList::CheckFitsHeaders(fitsfile* fptr, std::vector<std::string>& hdu_list) {
    // Use cfitsio lib to check headers for hdu list
    int hdunum, status;
    fits_get_num_hdus(fptr, &hdunum, &status);

    std::vector<int> valid_bitpix = {8, 16, 32, 64, -32, -64};

    for (int hdu = 0; hdu < hdunum; ++hdu) {
        // Add to hdulist if naxis > 0 and primary array or image extension
        // Common arguments for fits_read_key
        int status(0);
        char comment[70];

        if (hdu == 0) {
            // Check SIMPLE for primary header only; exit if false
            std::string key("SIMPLE");
            bool simple(false);
            fits_read_key(fptr, TLOGICAL, key.c_str(), &simple, comment, &status);
            if (status || !simple) {
                break;
            }
        }

        // Check NAXIS
        status = 0;
        std::string key("NAXIS");
        int naxis(0);
        fits_read_key(fptr, TINT, key.c_str(), &naxis, comment, &status);

        bool bitpix_valid(false);
        if (!status && (naxis > 0)) {
            // Check BITPIX
            key = "BITPIX";
            int bitpix(0);
            fits_read_key(fptr, TINT, key.c_str(), &bitpix, comment, &status);
            if (!status) {
                for (auto value : valid_bitpix) {
                    if (value == bitpix) {
                        bitpix_valid = true;
                        break;
                    }
                }
            }
        }

        if (bitpix_valid) {
            std::string hdu_name = std::to_string(hdu);
            if (hdu == 0) {
                // Add primary data array
                hdu_list.push_back(hdu_name);
            } else {
                // Check XTENSION; must be "IMAGE"
                key = "XTENSION";
                char xtension[70];
                fits_read_key(fptr, TSTRING, key.c_str(), xtension, comment, &status);

                if (!status) {
                    std::string ext_type(xtension);

                    if (ext_type.find("IMAGE") != std::string::npos) {
                        // Get extension name
                        key = "EXTNAME";
                        char extname[70];
                        fits_read_key(fptr, TSTRING, key.c_str(), extname, comment, &status);

                        if (!status) {
                            // Add hdu with ext name
                            std::string ext_name(extname);
                            std::string hdu_ext_name = fmt::format("{}: {}", hdu_name, ext_name);
                            hdu_list.push_back(hdu_ext_name);
                        } else {
                            // Add hdu
                            hdu_list.push_back(hdu_name);
                        }
                    }
                }
            }
        }

        // Move to next hdu
        int* hdutype(nullptr);
        fits_movrel_hdu(fptr, 1, hdutype, &status);
    }
}
