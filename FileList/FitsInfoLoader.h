#ifndef CARTA_BACKEND_FILELIST_FITSINFOLOADER_H_
#define CARTA_BACKEND_FILELIST_FITSINFOLOADER_H_

#include "FileInfoLoader.h"

#include <casacore/fits/FITS/hdu.h> // hdu.h must come before fitsio.h

#include <casacore/fits/FITS/FITSError.h>
#include <casacore/fits/FITS/fitsio.h>

inline void FitsInfoErrHandler(const char* err_message, casacore::FITSError::ErrorLevel severity) {
    if (severity > casacore::FITSError::WARN)
        std::cout << err_message << std::endl;
}

class FitsInfoLoader : public FileInfoLoader {
public:
    FitsInfoLoader(const std::string& filename);

protected:
    virtual CARTA::FileType GetCartaFileType() override;
    virtual bool GetHduList(CARTA::FileInfo* file_info, const std::string& abs_filename) override;
    virtual bool FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) override;

private:
    bool IsImageHdu(casacore::FITS::HDUType hdu_type);
    void GetFitsHeaderInfo(casacore::FitsInput& fits_input, int& ndim, std::string& ext_name);
};

FitsInfoLoader::FitsInfoLoader(const std::string& filename) {
    _filename = filename;
}

CARTA::FileType FitsInfoLoader::GetCartaFileType() {
    return CARTA::FileType::FITS;
}

bool FitsInfoLoader::GetHduList(CARTA::FileInfo* file_info, const std::string& abs_filename) {
    bool hdu_ok(false);
    casacore::FitsInput fits_input(abs_filename.c_str(), casacore::FITS::Disk, 10, FitsInfoErrHandler);
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
                        GetFitsHeaderInfo(fits_input, ndim, ext_name);
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
    }
    return hdu_ok;
}

bool FitsInfoLoader::IsImageHdu(casacore::FITS::HDUType hdu_type) {
    return ((hdu_type == casacore::FITS::PrimaryArrayHDU) || (hdu_type == casacore::FITS::PrimaryGroupHDU) ||
            (hdu_type == casacore::FITS::PrimaryTableHDU) || (hdu_type == casacore::FITS::ImageExtensionHDU));
}

void FitsInfoLoader::GetFitsHeaderInfo(casacore::FitsInput& fits_input, int& ndim, std::string& ext_name) {
    // return dims and extname from header
    switch (fits_input.hdutype()) {
        case casacore::FITS::PrimaryArrayHDU:
        case casacore::FITS::PrimaryGroupHDU:
        case casacore::FITS::PrimaryTableHDU: {
            casacore::HeaderDataUnit* header_unit(nullptr);
            switch (fits_input.datatype()) {
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

bool FitsInfoLoader::FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    bool ext_info_ok(true);
    try {
        // convert string hdu to unsigned int
        casacore::uInt hdu_num;
        try {
            casacore::String cc_hdu(hdu);
            cc_hdu = cc_hdu.before(" "); // strip ExtName
            cc_hdu.fromString(hdu_num, true);
        } catch (casacore::AipsError& err) {
            message = "Invalid hdu for FITS image.";
            return false;
        }

        // check shape
        casacore::Int num_dim(0);
        casacore::IPosition data_shape;
        try {
            casacore::FITSImage fits_img(_filename, 0, hdu_num);
            data_shape = fits_img.shape();
            num_dim = data_shape.size();
            if (num_dim < 2 || num_dim > 4) {
                message = "Image must be 2D, 3D or 4D.";
                return false;
            }
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
            if (message.find("diagonal") != std::string::npos) { // "ArrayBase::diagonal() - diagonal out of range"
                message = "Failed to open image at specified HDU.";
            } else if (message.find("No image at specified location") != std::string::npos) {
                message = "No image at specified HDU.";
            } else {
                message = "Failed to open image at specified HDU: " + message;
            }
            return false;
        }
        ext_info->set_dimensions(num_dim);

        // use FITSTable to get Record of hdu entries
        hdu_num += 1; // FITSTable starts at 1
        casacore::FITSTable fits_table(_filename, hdu_num, true);
        casacore::Record hdu_entries(fits_table.primaryKeywords().toRecord());
        // set dims
        ext_info->set_width(data_shape(0));
        ext_info->set_height(data_shape(1));
        ext_info->add_stokes_vals(""); // not in header

        // set missing dims in header entries
        if (!hdu_entries.isDefined("NAXIS")) {
            auto header_entry = ext_info->add_header_entries();
            header_entry->set_name("NAXIS");
            *header_entry->mutable_value() = fmt::format("{}", num_dim);
            header_entry->set_entry_type(CARTA::EntryType::INT);
            header_entry->set_numeric_value(num_dim);
        }
        for (int i = 0; i < num_dim; ++i) {
            casacore::String axis_name("NAXIS" + casacore::String::toString(i + 1));
            if (!hdu_entries.isDefined(axis_name)) {
                int naxis_i(data_shape(i));
                auto header_entry = ext_info->add_header_entries();
                header_entry->set_name(axis_name);
                *header_entry->mutable_value() = fmt::format("{}", naxis_i);
                header_entry->set_entry_type(CARTA::EntryType::INT);
                header_entry->set_numeric_value(naxis_i);
            }
        }

        // if in header, save values for computed entries
        std::string coord_type_x, coord_type_y, coord_type3, coord_type4;
        std::string rade_sys, equinox, spec_sys, bunit, crpix1, crpix2, cunit1, cunit2;
        double crval1(0.0), crval2(0.0), cdelt1(0.0), cdelt2(0.0), bmaj(0.0), bmin(0.0), bpa(0.0);

        // set header entries
        for (casacore::uInt field = 0; field < hdu_entries.nfields(); ++field) {
            casacore::String name = hdu_entries.name(field);
            if ((name != "SIMPLE") && (name != "BITPIX") && !name.startsWith("PC")) {
                auto header_entry = ext_info->add_header_entries();
                header_entry->set_name(name);
                casacore::DataType data_type(hdu_entries.type(field));
                switch (data_type) {
                    case casacore::TpString: {
                        *header_entry->mutable_value() = hdu_entries.asString(field);
                        header_entry->set_entry_type(CARTA::EntryType::STRING);
                        if (header_entry->name() == "CTYPE1")
                            coord_type_x = header_entry->value();
                        else if (header_entry->name() == "CTYPE2")
                            coord_type_y = header_entry->value();
                        else if (header_entry->name() == "CTYPE3")
                            coord_type3 = header_entry->value();
                        else if (header_entry->name() == "CTYPE4")
                            coord_type4 = header_entry->value();
                        else if (header_entry->name() == "RADESYS")
                            rade_sys = header_entry->value();
                        else if (header_entry->name() == "SPECSYS")
                            spec_sys = header_entry->value();
                        else if (header_entry->name() == "BUNIT")
                            bunit = header_entry->value();
                        else if (header_entry->name() == "CUNIT1")
                            cunit1 = header_entry->value();
                        else if (header_entry->name() == "CUNIT2")
                            cunit2 = header_entry->value();
                        break;
                    }
                    case casacore::TpInt: {
                        int64_t value_int(hdu_entries.asInt(field));
                        if ((name == "NAXIS") && (value_int == 0))
                            value_int = num_dim;
                        *header_entry->mutable_value() = fmt::format("{}", value_int);
                        header_entry->set_entry_type(CARTA::EntryType::INT);
                        header_entry->set_numeric_value(value_int);
                        break;
                    }
                    case casacore::TpFloat:
                    case casacore::TpDouble: {
                        double numeric_value(hdu_entries.asDouble(field));
                        *header_entry->mutable_value() = fmt::format("{}", numeric_value);
                        header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                        header_entry->set_numeric_value(numeric_value);
                        if (header_entry->name() == "EQUINOX")
                            equinox = std::to_string(static_cast<int>(numeric_value));
                        else if (header_entry->name() == "CRVAL1")
                            crval1 = numeric_value;
                        else if (header_entry->name() == "CRVAL2")
                            crval2 = numeric_value;
                        else if (header_entry->name() == "CRPIX1")
                            crpix1 = std::to_string(static_cast<int>(numeric_value));
                        else if (header_entry->name() == "CRPIX2")
                            crpix2 = std::to_string(static_cast<int>(numeric_value));
                        else if (header_entry->name() == "CDELT1")
                            cdelt1 = numeric_value;
                        else if (header_entry->name() == "CDELT2")
                            cdelt2 = numeric_value;
                        else if (header_entry->name() == "BMAJ")
                            bmaj = numeric_value;
                        else if (header_entry->name() == "BMIN")
                            bmin = numeric_value;
                        else if (header_entry->name() == "BPA")
                            bpa = numeric_value;
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        // shape, chan, stokes entries first
        int chan_axis, stokes_axis;
        FindChanStokesAxis(data_shape, coord_type_x, coord_type_y, coord_type3, coord_type4, chan_axis, stokes_axis);
        AddShapeEntries(ext_info, data_shape, chan_axis, stokes_axis);

        // make strings for computed entries
        std::string xy_coords, cr_pixels, cr_coords, cr1, cr2, cr_deg_str, axis_inc, rs_beam;
        if (!coord_type_x.empty() && !coord_type_y.empty())
            xy_coords = fmt::format("{}, {}", coord_type_x, coord_type_y);
        if (!crpix1.empty() && !crpix2.empty())
            cr_pixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (crval1 != 0.0 && crval2 != 0.0)
            cr_coords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval1, cunit1, crval2, cunit2);
        cr1 = MakeValueStr(coord_type_x, crval1, cunit1);
        cr2 = MakeValueStr(coord_type_y, crval2, cunit2);
        cr_deg_str = fmt::format("[{}, {}]", cr1, cr2);
        if (!(cdelt1 == 0.0 && cdelt2 == 0.0))
            axis_inc = fmt::format("{}, {}", UnitConversion(cdelt1, cunit1), UnitConversion(cdelt2, cunit2));
        if (!(bmaj == 0.0 && bmin == 0.0 && bpa == 0.0))
            rs_beam = Deg2Arcsec(bmaj) + " X " + Deg2Arcsec(bmin) + fmt::format(", {:.4f} deg", bpa);
        MakeRadeSysStr(rade_sys, equinox);

        // fill computed_entries
        AddComputedEntries(ext_info, xy_coords, cr_pixels, cr_coords, cr_deg_str, rade_sys, spec_sys, bunit, axis_inc, rs_beam);
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
        ext_info_ok = false;
    }
    return ext_info_ok;
}

#endif // CARTA_BACKEND_FILELIST_FITSINFOLOADER_H_
