/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// FitsHduList.cc: Fill FileInfo HDU_list with hdu number and extension name

#include "FitsHduList.h"

FitsHduList::FitsHduList(const std::string& filename) {
    _filename = filename;
}

void FitsHduList::GetHduList(std::vector<std::string>& hdu_list) {
    casacore::FitsInput fits_input(_filename.c_str(), casacore::FITS::Disk, 10, FitsInfoErrHandler);

    auto input_error = fits_input.err();
    if (input_error == casacore::FitsIO::OK) {
        // iterate through each header data unit
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
                    // Check for FITS compressed fz: header ZIMAGE = T.
                    // casacore::BinaryTableExtension destructor gets seg fault.
                    // Using ExtensionHeaderDataUnit generates error "Input does not contain an HDU of this type" (UnknownExtensionHDU)
                    casacore::ExtensionHeaderDataUnit extension(fits_input, FitsInfoErrHandler);
                    casacore::FitsKeywordList fkw_list;
                    if (!extension.get_hdr(hdu_type, fkw_list)) {
                        casacore::FitsKeyword* fkw = fkw_list("ZIMAGE");
                        if (fkw && fkw->asBool()) {
                            // Check ndim
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
    } else {
        switch (input_error) {
            case casacore::FitsIO::EMPTYFILE:
                FitsInfoErrHandler("FITS file is empty", casacore::FITSError::SEVERE);
                break;
            case casacore::FitsIO::IOERR:
                FitsInfoErrHandler("FITS read error", casacore::FITSError::SEVERE);
                break;
            case casacore::FitsIO::BADBEGIN:
                FitsInfoErrHandler(
                    "FITS invalid/unsupported primary header (SIMPLE, XTENSION, BITPIX, NAXIS, or NAXIS1).", casacore::FITSError::SEVERE);
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
    // return dims and extname from header
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
                case casacore::FITS::COMPLEX:
                    header_unit = new casacore::PrimaryArray<std::complex<float>>(fits_input);
                    break;
                case casacore::FITS::DCOMPLEX:
                    header_unit = new casacore::PrimaryArray<std::complex<double>>(fits_input);
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
                case casacore::FITS::COMPLEX: {
                    casacore::ImageExtension<std::complex<float>> header_unit = casacore::ImageExtension<std::complex<float>>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                } break;
                case casacore::FITS::DCOMPLEX: {
                    casacore::ImageExtension<std::complex<double>> header_unit = casacore::ImageExtension<std::complex<double>>(fits_input);
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
