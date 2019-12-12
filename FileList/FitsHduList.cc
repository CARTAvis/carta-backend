// FitsHduList.cc: Fill FileInfo HDU_list with hdu number and extension name

#include "FitsHduList.h"

FitsHduList::FitsHduList(const std::string& filename) {
    _filename = filename;
}

bool FitsHduList::GetHduList(CARTA::FileInfo* file_info) {
    bool hdu_ok(false);
    casacore::FitsInput fits_input(_filename.c_str(), casacore::FITS::Disk, 10, FitsInfoErrHandler);
    if (fits_input.err() == casacore::FitsIO::OK) { // check for cfitsio error
        int num_hdu(fits_input.getnumhdu());
        hdu_ok = (num_hdu > 0);
        if (hdu_ok) {
            // iterate through each header data unit
            for (int hdu = 0; hdu < num_hdu; ++hdu) {
                if (fits_input.rectype() == casacore::FITS::HDURecord) {
                    casacore::FITS::HDUType hdu_type = fits_input.hdutype();
                    if (IsImageHdu(hdu_type)) {
                        // add hdu to file info
                        std::string hdu_name = std::to_string(hdu);
                        int ndim(0);
                        std::string ext_name;
                        GetFitsHduInfo(fits_input, ndim, ext_name);
                        if (ndim > 0) {
                            if (!ext_name.empty()) {
                                hdu_name += " ExtName: " + ext_name;
                            }
                            file_info->add_hdu_list(hdu_name);
                        }
                        fits_input.skip_all(hdu_type); // skip data to next hdu
                    } else {
                        fits_input.skip_hdu();
                    }
                }
            }
        }
    } else {
        std::cerr << "FitsInput error for " << _filename << std::endl;
    }
    return hdu_ok;
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
