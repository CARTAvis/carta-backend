//# FileInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileInfoLoader.h"

#include <algorithm>
#include <regex>

#include <fmt/format.h>

#include <casacore/casa/HDF5/HDF5Error.h>
#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/casa/OS/File.h>
#include <casacore/fits/FITS/FITSTable.h>
#include <casacore/images/Images/FITSImage.h>
#include <casacore/images/Images/ImageSummary.h>
#include <casacore/images/Images/MIRIADImage.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/lattices/Lattices/HDF5Lattice.h>
#include <casacore/mirlib/miriad.h>

#include "ImageData/Hdf5Attributes.h"

//#################################################################################
// FILE INFO LOADER

FileInfoLoader::FileInfoLoader(const std::string& filename) : _filename(filename) {
    _image_type = FileType(filename);
}

casacore::ImageOpener::ImageTypes FileInfoLoader::FileType(const std::string& file) {
    return casacore::ImageOpener::imageType(file);
}

//#################################################################################
// FILE INFO

bool FileInfoLoader::FillFileInfo(CARTA::FileInfo* file_info) {
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }

    // fill FileInfo submessage
    int64_t file_info_size(cc_file.size());
    if (cc_file.isDirectory()) { // symlinked dirs are dirs
        casacore::Directory cc_dir(cc_file);
        file_info_size = cc_dir.size();
    } else if (cc_file.isSymLink()) { // gets size of link not file
        casacore::String resolved_file_name(cc_file.path().resolvedName());
        casacore::File linked_file(resolved_file_name);
        file_info_size = linked_file.size();
    }

    file_info->set_size(file_info_size);
    file_info->set_type(ConvertFileType(_image_type));
    casacore::String abs_file_name(cc_file.path().absoluteName());
    return GetHduList(file_info, abs_file_name);
}

CARTA::FileType FileInfoLoader::ConvertFileType(int cc_image_type) {
    // convert casacore ImageType to protobuf FileType
    switch (cc_image_type) {
        case casacore::ImageOpener::FITS:
            return CARTA::FileType::FITS;
        case casacore::ImageOpener::AIPSPP:
            return CARTA::FileType::CASA;
        case casacore::ImageOpener::HDF5:
            return CARTA::FileType::HDF5;
        case casacore::ImageOpener::MIRIAD:
            return CARTA::FileType::MIRIAD;
        default:
            return CARTA::FileType::UNKNOWN;
    }
}

bool FileInfoLoader::GetHduList(CARTA::FileInfo* file_info, const std::string& filename) {
    // fill FileInfo hdu list
    bool hdu_ok(false);
    if (file_info->type() == CARTA::FileType::HDF5) {
        casacore::HDF5File hdf_file(filename);
        std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdf_file));
        if (hdus.empty()) {
            file_info->add_hdu_list("");
            hdu_ok = true;
        } else {
            for (auto group_name : hdus)
                file_info->add_hdu_list(group_name);
            hdu_ok = true;
        }
    } else if (file_info->type() == CARTA::FITS) {
        casacore::FitsInput fits_input(filename.c_str(), casacore::FITS::Disk, 10, FileInfoFitsErrHandler);
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
    } else {
        file_info->add_hdu_list("");
        hdu_ok = true;
    }
    return hdu_ok;
}

bool FileInfoLoader::IsImageHdu(casacore::FITS::HDUType hdu_type) {
    return ((hdu_type == casacore::FITS::PrimaryArrayHDU) || (hdu_type == casacore::FITS::PrimaryGroupHDU) ||
            (hdu_type == casacore::FITS::PrimaryTableHDU) || (hdu_type == casacore::FITS::ImageExtensionHDU));
}

void FileInfoLoader::GetFitsHeaderInfo(casacore::FitsInput& fits_input, int& ndim, std::string& ext_name) {
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
                }   break;
                case casacore::FITS::SHORT: {
                    casacore::ImageExtension<short> header_unit = casacore::ImageExtension<short>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                }   break;
                case casacore::FITS::LONG: {
                    casacore::ImageExtension<int> header_unit = casacore::ImageExtension<int>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                }   break;
                case casacore::FITS::FLOAT: {
                    casacore::ImageExtension<float> header_unit = casacore::ImageExtension<float>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                }   break;
                case casacore::FITS::DOUBLE: {
                    casacore::ImageExtension<double> header_unit = casacore::ImageExtension<double>(fits_input);
                    ndim = header_unit.dims();
                    ext_name = header_unit.extname();
                }   break;
                default:
                    break;
            }
        } break;
        default:
            break;
    }
}

//#################################################################################
// FILE INFO EXTENDED

bool FileInfoLoader::FillFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }

    bool ext_info_ok(false);
    std::string name(cc_file.path().baseName());
    auto entry = ext_info->add_computed_entries();
    entry->set_name("Name");
    entry->set_value(name);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // fill FileExtInfo depending on image type
    switch (_image_type) {
        case casacore::ImageOpener::AIPSPP:
            ext_info_ok = FillCasaExtFileInfo(ext_info, message);
            break;
        case casacore::ImageOpener::FITS:
            ext_info_ok = FillFitsExtFileInfo(ext_info, hdu, message);
            break;
        case casacore::ImageOpener::HDF5:
            ext_info_ok = FillHdf5ExtFileInfo(ext_info, hdu, message);
            break;
        case casacore::ImageOpener::MIRIAD:
            ext_info_ok = FillCasaExtFileInfo(ext_info, message);
            break;
        default:
            break;
    }
    return ext_info_ok;
}

// HDF5
bool FileInfoLoader::FillHdf5ExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    // Add extended info for HDF5 file
    try {
        casacore::HDF5File hdf_file(_filename);
        if (hdu.empty()) { // use first
            hdu = casacore::HDF5Group::linkNames(hdf_file)[0];
        }
        casacore::HDF5Group hdf_group(hdf_file, hdu, true);
        casacore::Record attributes;
        try {
            // read attributes into casacore Record
            attributes = Hdf5Attributes::ReadAttributes(hdf_group.getHid());
        } catch (casacore::HDF5Error& err) {
            message = "Error reading HDF5 attributes: " + err.getMesg();
            hdf_group.close();
            hdf_file.close();
            return false;
        }
        hdf_group.close();
        hdf_file.close();
        if (attributes.empty()) {
            message = "No HDF5 attributes";
            return false;
        }

        // check dimensions
        casacore::uInt num_dims;
        casacore::IPosition data_shape;
        try {
            casacore::HDF5Lattice<float> hdf5_lattice = casacore::HDF5Lattice<float>(_filename, "DATA", hdu);
            data_shape = hdf5_lattice.shape();
            num_dims = data_shape.size();
        } catch (casacore::AipsError& err) {
            message = "Cannot open HDF5 DATA dataset.";
            return false;
        }
        if (num_dims < 2 || num_dims > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }

        // extract values from Record
        for (casacore::uInt field = 0; field < attributes.nfields(); ++field) {
            auto header_entry = ext_info->add_header_entries();
            header_entry->set_name(attributes.name(field));
            switch (attributes.type(field)) {
                case casacore::TpString: {
                    *header_entry->mutable_value() = attributes.asString(field);
                    header_entry->set_entry_type(CARTA::EntryType::STRING);
                } break;
                case casacore::TpBool: {
                    casacore::Bool value_bool = attributes.asBool(field);
                    *header_entry->mutable_value() = fmt::format("{}", value_bool);
                    header_entry->set_entry_type(CARTA::EntryType::INT);
                    header_entry->set_numeric_value(value_bool);
                } break;
                case casacore::TpInt:
                case casacore::TpInt64: {
                    casacore::Int64 value_int = attributes.asInt64(field);
                    *header_entry->mutable_value() = fmt::format("{}", value_int);
                    header_entry->set_entry_type(CARTA::EntryType::INT);
                    header_entry->set_numeric_value(value_int);
                } break;
                case casacore::TpDouble: {
                    casacore::Double numeric_val = attributes.asDouble(field);
                    *header_entry->mutable_value() = fmt::format("{}", numeric_val);
                    header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                    header_entry->set_numeric_value(numeric_val);
                } break;
                default:
                    break;
            }
        }

        // If in header, get values for computed entries:
        // Get string values
        std::string coord_type_x = (attributes.isDefined("CTYPE1") ? attributes.asString("CTYPE1") : "");
        std::string coord_type_y = (attributes.isDefined("CTYPE2") ? attributes.asString("CTYPE2") : "");
        std::string coord_type_3 = (attributes.isDefined("CTYPE3") ? attributes.asString("CTYPE3") : "");
        std::string coord_type_4 = (attributes.isDefined("CTYPE4") ? attributes.asString("CTYPE4") : "");
        std::string rade_sys = (attributes.isDefined("RADESYS") ? attributes.asString("RADESYS") : "");
        std::string equinox = GetStringAttribute(attributes, "EQUINOX");
        std::string spec_sys = (attributes.isDefined("SPECSYS") ? attributes.asString("SPECSYS") : "");
        std::string bunit = (attributes.isDefined("BUNIT") ? attributes.asString("BUNIT") : "");
        std::string crpix1 = GetStringAttribute(attributes, "CRPIX1");
        std::string crpix2 = GetStringAttribute(attributes, "CRPIX2");
        std::string cunit1 = (attributes.isDefined("CUNIT1") ? attributes.asString("CUNIT1") : "");
        std::string cunit2 = (attributes.isDefined("CUNIT2") ? attributes.asString("CUNIT2") : "");
        // Get numeric values
        double crval1 = GetDoubleAttribute(attributes, "CRVAL1");
        double crval2 = GetDoubleAttribute(attributes, "CRVAL2");
        double cdelt1 = GetDoubleAttribute(attributes, "CDELT1");
        double cdelt2 = GetDoubleAttribute(attributes, "CDELT2");
        double bmaj = GetDoubleAttribute(attributes, "BMAJ");
        double bmin = GetDoubleAttribute(attributes, "BMIN");
        double bpa = GetDoubleAttribute(attributes, "BPA");

        // shape, chan, stokes entries first
        int chan_axis, stokes_axis;
        FindChanStokesAxis(data_shape, coord_type_x, coord_type_y, coord_type_3, coord_type_4, chan_axis, stokes_axis);
        AddShapeEntries(ext_info, data_shape, chan_axis, stokes_axis);
        ext_info->add_stokes_vals(""); // not in header

        // make computed entries strings
        std::string xy_coords, cr_pixels, cr_coords, cr_deg_str, cr1, cr2, axis_inc, rs_beam;
        if (!coord_type_x.empty() && !coord_type_y.empty())
            xy_coords = fmt::format("{}, {}", coord_type_x, coord_type_y);
        if (!crpix1.empty() && !crpix2.empty())
            cr_pixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (!(crval1 == 0.0 && crval2 == 0.0))
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
        message = "Error opening HDF5 file";
        return false;
    }
    return true;
}

std::string FileInfoLoader::GetStringAttribute(casacore::Record& record, std::string field) {
    // return attribute as string
    std::string value;
    if (record.isDefined(field)) {
        switch (record.type(record.fieldNumber(field))) { // Hdf5Attributes only uses these types
            case casacore::TpString: {
                value = record.asString(field);
            } break;
            case casacore::TpInt64: {
                value = std::to_string(record.asInt64(field));
            } break;
            case casacore::TpDouble: {
                value = std::to_string(record.asDouble(field));
            } break;
            default:
                break;
        }
    }
    return value;
}

double FileInfoLoader::GetDoubleAttribute(casacore::Record& record, std::string field) {
    // return attribute as double
    double value(0.0);
    if (record.isDefined(field)) {
        switch (record.type(record.fieldNumber(field))) { // Hdf5Attributes only uses these types
            case casacore::TpString: {
                value = stod(record.asString(field));
            } break;
            case casacore::TpInt64: {
                value = static_cast<double>(record.asInt64(field));
            } break;
            case casacore::TpDouble: {
                value = record.asDouble(field);
            } break;
            default:
                break;
        }
    }
    return value;
}

// FITS
bool FileInfoLoader::FillFitsExtFileInfo(CARTA::FileInfoExtended* ext_info, string& hdu, string& message) {
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

// CASA, MIRIAD
bool FileInfoLoader::FillCasaExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& message) {
    bool ext_info_ok(true);
    casacore::ImageInterface<casacore::Float>* cc_image(nullptr);
    try {
        switch (_image_type) {
            case casacore::ImageOpener::AIPSPP: {
                cc_image = new casacore::PagedImage<casacore::Float>(_filename);
                break;
            }
            case casacore::ImageOpener::MIRIAD: {
                // no way to catch error, use casacore miriad lib directly
                int t_handle, i_handle, io_stat, num_dim;
                hopen_c(&t_handle, _filename.c_str(), "old", &io_stat);
                if (io_stat != 0) {
                    message = "Could not open MIRIAD file";
                    return false;
                }
                haccess_c(t_handle, &i_handle, "image", "read", &io_stat);
                if (io_stat != 0) {
                    message = "Could not open MIRIAD file";
                    return false;
                }
                rdhdi_c(t_handle, "naxis", &num_dim, 0); // read "naxis" value into ndim, default 0
                hdaccess_c(i_handle, &io_stat);
                hclose_c(t_handle);
                if (num_dim < 2 || num_dim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return false;
                }
                // hopefully okay to open now as casacore Image
                cc_image = new casacore::MIRIADImage(_filename);
                break;
            }
            default:
                break;
        }
        if (cc_image == nullptr) {
            message = "Unable to open image.";
            return false;
        }
        // objects to retrieve header information
        casacore::ImageInfo image_info(cc_image->imageInfo());
        casacore::ImageSummary<casacore::Float> image_summary(*cc_image);
        casacore::CoordinateSystem coord_sys(cc_image->coordinates());

        // num_dims, shape
        casacore::Int num_dims(image_summary.ndim());
        ext_info->set_dimensions(num_dims);
        if (num_dims < 2 || num_dims > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }
        casacore::IPosition data_shape(image_summary.shape());
        ext_info->set_width(data_shape(0));
        ext_info->set_height(data_shape(1));
        ext_info->add_stokes_vals(""); // not in header
        // set dims in header entries
        auto num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("NAXIS");
        *num_axis_header_entry->mutable_value() = fmt::format("{}", num_dims);
        num_axis_header_entry->set_entry_type(CARTA::EntryType::INT);
        num_axis_header_entry->set_numeric_value(num_dims);
        for (casacore::Int i = 0; i < num_dims; ++i) {
            auto header_entry = ext_info->add_header_entries();
            header_entry->set_name("NAXIS" + casacore::String::toString(i + 1));
            *header_entry->mutable_value() = fmt::format("{}", data_shape(i));
            header_entry->set_entry_type(CARTA::EntryType::INT);
            header_entry->set_numeric_value(data_shape(i));
        }

        // if in header, save values for computed entries
        std::string rs_beam, bunit, projection, equinox, rade_sys, coord_type_x, coord_type_y, coord_type3, coord_type4, spec_sys;

        // BMAJ, BMIN, BPA
        if (image_info.hasBeam() && image_info.hasSingleBeam()) {
            // get values
            casacore::GaussianBeam rbeam(image_info.restoringBeam());
            casacore::Quantity major_axis(rbeam.getMajor()), minor_axis(rbeam.getMinor()), pa(rbeam.getPA(true));
            major_axis.convert("deg");
            minor_axis.convert("deg");
            pa.convert("deg");

            // add to header entries
            casacore::Double bmaj(major_axis.getValue()), bmin(minor_axis.getValue());
            casacore::Float bpa(pa.getValue());
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("BMAJ");
            *num_axis_header_entry->mutable_value() = fmt::format("{}", bmaj);
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(bmaj);
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("BMIN");
            *num_axis_header_entry->mutable_value() = fmt::format("{}", bmin);
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(bmin);
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("BPA");
            *num_axis_header_entry->mutable_value() = fmt::format("{}", bpa);
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(bpa);

            // add to computed entries
            rs_beam = fmt::format("{} X {}, {:.4f} deg", Deg2Arcsec(bmaj), Deg2Arcsec(bmin), bpa);
        }

        // type
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("BTYPE");
        *num_axis_header_entry->mutable_value() = casacore::ImageInfo::imageType(image_info.imageType());
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        // object
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("OBJECT");
        *num_axis_header_entry->mutable_value() = image_info.objectName();
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        // units
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("BUNIT");
        bunit = image_summary.units().getName();
        *num_axis_header_entry->mutable_value() = bunit;
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);

        // Direction axes: projection, equinox, radesys
        casacore::Vector<casacore::String> dir_axis_names; // axis names to append projection
        casacore::MDirection::Types dir_type;
        casacore::Int i_dir_coord(coord_sys.findCoordinate(casacore::Coordinate::DIRECTION));
        if (i_dir_coord >= 0) {
            const casacore::DirectionCoordinate& dir_coord = coord_sys.directionCoordinate(casacore::uInt(i_dir_coord));
            // direction axes
            projection = dir_coord.projection().name();
            dir_axis_names = dir_coord.worldAxisNames();
            // equinox, radesys
            dir_coord.getReferenceConversion(dir_type);
            equinox = casacore::MDirection::showType(dir_type);
            GetRadeSysFromEquinox(rade_sys, equinox);
        }

        // get imSummary axes values: name, reference pixel and value, pixel increment, units
        casacore::Vector<casacore::String> ax_names(image_summary.axisNames());
        casacore::Vector<casacore::Double> ax_ref_pix(image_summary.referencePixels());
        casacore::Vector<casacore::Double> ax_ref_val(image_summary.referenceValues());
        casacore::Vector<casacore::Double> ax_increments(image_summary.axisIncrements());
        casacore::Vector<casacore::String> ax_units(image_summary.axisUnits());
        size_t axis_size(ax_names.size());
        for (casacore::uInt i = 0; i < axis_size; ++i) {
            casacore::String suffix(casacore::String::toString(i + 1)); // append to keyword name
            casacore::String axis_name = ax_names(i);
            // modify direction axes, if any
            if ((i_dir_coord >= 0) && anyEQ(dir_axis_names, axis_name)) {
                ConvertAxisName(axis_name, projection, dir_type); // FITS name with projection
                if (ax_units(i) == "rad") {                       // convert ref value and increment to deg
                    casacore::Quantity ref_val_quant(ax_ref_val(i), ax_units(i));
                    ref_val_quant.convert("deg");
                    casacore::Quantity increment_quant(ax_increments(i), ax_units(i));
                    increment_quant.convert("deg");
                    // update values to use later for computed entries
                    ax_ref_val(i) = ref_val_quant.getValue();
                    ax_increments(i) = increment_quant.getValue();
                    ax_units(i) = increment_quant.getUnit();
                }
            }
            // name = CTYPE
            // add header entry
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("CTYPE" + suffix);
            num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
            *num_axis_header_entry->mutable_value() = axis_name;
            // computed entries
            if (suffix == "1")
                coord_type_x = axis_name;
            else if (suffix == "2")
                coord_type_y = axis_name;
            else if (suffix == "3")
                coord_type3 = axis_name;
            else if (suffix == "4")
                coord_type4 = axis_name;

            // ref val = CRVAL
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("CRVAL" + suffix);
            *num_axis_header_entry->mutable_value() = fmt::format("{}", ax_ref_val(i));
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(ax_ref_val(i));
            // increment = CDELT
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("CDELT" + suffix);
            *num_axis_header_entry->mutable_value() = fmt::format("{}", ax_increments(i));
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(ax_increments(i));
            // ref pix = CRPIX
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("CRPIX" + suffix);
            *num_axis_header_entry->mutable_value() = fmt::format("{}", ax_ref_pix(i));
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(ax_ref_pix(i));
            // units = CUNIT
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("CUNIT" + suffix);
            *num_axis_header_entry->mutable_value() = ax_units(i);
            num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        }

        // RESTFRQ
        casacore::String rest_freq_string;
        casacore::Quantum<casacore::Double> rest_freq;
        casacore::Bool ok = image_summary.restFrequency(rest_freq_string, rest_freq);
        if (ok) {
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("RESTFRQ");
            casacore::Double rest_freq_val(rest_freq.getValue());
            *num_axis_header_entry->mutable_value() = rest_freq_string;
            num_axis_header_entry->set_entry_type(CARTA::EntryType::FLOAT);
            num_axis_header_entry->set_numeric_value(rest_freq_val);
        }

        // SPECSYS
        casacore::Int i_spec_coord(coord_sys.findCoordinate(casacore::Coordinate::SPECTRAL));
        if (i_spec_coord >= 0) {
            const casacore::SpectralCoordinate& spectral_coordinate = coord_sys.spectralCoordinate(casacore::uInt(i_spec_coord));
            casacore::MFrequency::Types freq_type;
            casacore::MEpoch epoch;
            casacore::MPosition position;
            casacore::MDirection direction;
            spectral_coordinate.getReferenceConversion(freq_type, epoch, position, direction);
            bool have_spec_sys = ConvertSpecSysToFits(spec_sys, freq_type);
            if (have_spec_sys) {
                num_axis_header_entry = ext_info->add_header_entries();
                num_axis_header_entry->set_name("SPECSYS");
                *num_axis_header_entry->mutable_value() = spec_sys;
                num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        // RADESYS, EQUINOX
        if (!rade_sys.empty()) {
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("RADESYS");
            *num_axis_header_entry->mutable_value() = rade_sys;
            num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        }
        if (!equinox.empty()) {
            num_axis_header_entry = ext_info->add_header_entries();
            num_axis_header_entry->set_name("EQUINOX");
            *num_axis_header_entry->mutable_value() = equinox;
            num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        }
        // computed entries
        MakeRadeSysStr(rade_sys, equinox);

        // Other summary items
        // telescope
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("TELESCOP");
        *num_axis_header_entry->mutable_value() = image_summary.telescope();
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        // observer
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("OBSERVER");
        *num_axis_header_entry->mutable_value() = image_summary.observer();
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);
        // obs date
        casacore::MEpoch epoch;
        num_axis_header_entry = ext_info->add_header_entries();
        num_axis_header_entry->set_name("DATE");
        *num_axis_header_entry->mutable_value() = image_summary.obsDate(epoch);
        num_axis_header_entry->set_entry_type(CARTA::EntryType::STRING);

        // shape, chan, stokes entries first
        int chan_axis, stokes_axis;
        FindChanStokesAxis(data_shape, coord_type_x, coord_type_y, coord_type3, coord_type4, chan_axis, stokes_axis);
        AddShapeEntries(ext_info, data_shape, chan_axis, stokes_axis);

        // computed_entries
        std::string xy_coords, cr_pixels, cr_coords, cr_deg_str, axis_inc;
        int cr_pix0, cr_pix1;
        std::string cunit0, cr0, cunit1, cr1;
        double crval0, crval1, cdelt0, cdelt1;
        if (axis_size >= 1) { // pv images only have first axis values
            cr_pix0 = static_cast<int>(ax_ref_pix(0));
            cunit0 = ax_units(0);
            crval0 = ax_ref_val(0);
            cdelt0 = ax_increments(0);
            cr0 = MakeValueStr(coord_type_x, crval0, cunit0); // for angle format
            if (axis_size > 1) {
                cr_pix1 = static_cast<int>(ax_ref_pix(1));
                cunit1 = ax_units(1);
                crval1 = ax_ref_val(1);
                cdelt1 = ax_increments(1);
                cr1 = MakeValueStr(coord_type_y, crval1, cunit1); // for angle format
                xy_coords = fmt::format("{}, {}", coord_type_x, coord_type_y);
                cr_pixels = fmt::format("[{}, {}]", cr_pix0, cr_pix1);
                cr_coords = fmt::format("[{:.3f} {}, {:.3f} {}]", crval0, cunit0, crval1, cunit1);
                cr_deg_str = fmt::format("[{} {}]", cr0, cr1);
                axis_inc = fmt::format("{}, {}", UnitConversion(cdelt0, cunit0), UnitConversion(cdelt1, cunit1));
            } else {
                xy_coords = fmt::format("{}", coord_type_x);
                cr_pixels = fmt::format("[{}]", cr_pix0);
                cr_coords = fmt::format("[{:.3f} {}]", crval0, cunit0);
                cr_deg_str = fmt::format("[{}]", cr0);
                axis_inc = fmt::format("{}", UnitConversion(cdelt0, cunit0));
            }
        }
        AddComputedEntries(ext_info, xy_coords, cr_pixels, cr_coords, cr_deg_str, rade_sys, spec_sys, bunit, axis_inc, rs_beam);
    } catch (casacore::AipsError& err) {
        delete cc_image;
        message = err.getMesg().c_str();
        ext_info_ok = false;
    }

    delete cc_image;
    return ext_info_ok;
}

// ***** Computed entries *****

void FileInfoLoader::AddComputedEntries(CARTA::FileInfoExtended* ext_info, const std::string& xy_coords, const std::string& cr_pixels,
    const std::string& cr_coords, const std::string& cr_ra_dec, const std::string& rade_sys, const std::string& spec_sys,
    const std::string& bunit, const std::string& axis_inc, const std::string& rs_beam) {
    // add computed_entries to extended info (ensures the proper order in file browser)
    if (!xy_coords.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Coordinate type");
        entry->set_value(xy_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!cr_pixels.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Image reference pixels");
        entry->set_value(cr_pixels);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!cr_coords.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Image reference coordinates");
        entry->set_value(cr_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!cr_ra_dec.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Image ref coords (coord type)");
        entry->set_value(cr_ra_dec);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!rade_sys.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(rade_sys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!spec_sys.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(spec_sys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!bunit.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Pixel unit");
        entry->set_value(bunit);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!axis_inc.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Pixel increment");
        entry->set_value(axis_inc);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!rs_beam.empty()) {
        auto entry = ext_info->add_computed_entries();
        entry->set_name("Restoring beam");
        entry->set_value(rs_beam);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }
}

// shape, nchan, nstokes
void FileInfoLoader::AddShapeEntries(
    CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis) {
    // Set fields/header entries for shape

    // dim, width, height, depth, stokes fields
    int num_dims(shape.size());
    extended_info->set_dimensions(num_dims);
    extended_info->set_width(shape(0));
    extended_info->set_height(shape(1));
    if (shape.size() == 2) {
        extended_info->set_depth(1);
        extended_info->set_stokes(1);
    } else if (shape.size() == 3) {
        extended_info->set_depth(shape(2));
        extended_info->set_stokes(1);
    } else {                       // shape is 4
        if (chan_axis < 2) {       // not found or is xy
            if (stokes_axis < 2) { // not found or is xy, use defaults
                extended_info->set_depth(shape(2));
                extended_info->set_stokes(shape(3));
            } else { // stokes found, set depth to other one
                extended_info->set_stokes(shape(stokes_axis));
                if (stokes_axis == 2)
                    extended_info->set_depth(shape(3));
                else
                    extended_info->set_depth(shape(2));
            }
        } else if (chan_axis >= 2) { // chan found, set stokes to other one
            extended_info->set_depth(shape(chan_axis));
            // stokes axis is the other one
            if (chan_axis == 2)
                extended_info->set_stokes(shape(3));
            else
                extended_info->set_stokes(shape(2));
        }
    }

    // shape entry
    std::string shape_string;
    switch (num_dims) {
        case 2:
            shape_string = fmt::format("[{}, {}]", shape(0), shape(1));
            break;
        case 3:
            shape_string = fmt::format("[{}, {}, {}]", shape(0), shape(1), shape(2));
            break;
        case 4:
            shape_string = fmt::format("[{}, {}, {}, {}]", shape(0), shape(1), shape(2), shape(3));
            break;
    }
    auto shape_entry = extended_info->add_computed_entries();
    shape_entry->set_name("Shape");
    shape_entry->set_value(shape_string);
    shape_entry->set_entry_type(CARTA::EntryType::STRING);

    // nchan, nstokes computed entries
    // set number of channels if chan axis exists or has 3rd axis
    if ((chan_axis >= 0) || (num_dims >= 3)) {
        int nchan;
        if (chan_axis >= 0)
            nchan = shape(chan_axis);
        else
            nchan = extended_info->depth();
        // header entry for number of chan
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(casacore::String::toString(nchan));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nchan);
    }
    // set number of stokes if stokes axis exists or has 4th axis
    if ((stokes_axis >= 0) || (num_dims > 3)) {
        int num_stokes;
        if (stokes_axis >= 0)
            num_stokes = shape(stokes_axis);
        else
            num_stokes = extended_info->stokes();
        // header entry for number of stokes
        auto entry = extended_info->add_computed_entries();
        entry->set_name("Number of Stokes");
        entry->set_value(casacore::String::toString(num_stokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(num_stokes);
    }
}

// For shape entries, determine spectral and stokes axes
void FileInfoLoader::FindChanStokesAxis(const casacore::IPosition& data_shape, const casacore::String& axis_type_1,
    const casacore::String& axis_type_2, const casacore::String& axis_type_3, const casacore::String& axis_type_4, int& chan_axis,
    int& stokes_axis) {
    // Use CTYPE values to find axes and set nchan, nstokes
    // Note header axes are 1-based but shape is 0-based
    casacore::String c_type1(axis_type_1), c_type2(axis_type_2), c_type3(axis_type_3), c_type4(axis_type_4);
    // uppercase for string comparisons
    c_type1.upcase();
    c_type2.upcase();
    c_type3.upcase();
    c_type4.upcase();

    // find spectral axis
    if (!c_type1.empty() && (c_type1.contains("FELO") || c_type1.contains("FREQ") || c_type1.contains("VELO") || c_type1.contains("VOPT") ||
                                c_type1.contains("VRAD") || c_type1.contains("WAVE") || c_type1.contains("AWAV"))) {
        chan_axis = 0;
    } else if (!c_type2.empty() &&
               (c_type2.contains("FELO") || c_type2.contains("FREQ") || c_type2.contains("VELO") || c_type2.contains("VOPT") ||
                   c_type2.contains("VRAD") || c_type2.contains("WAVE") || c_type2.contains("AWAV"))) {
        chan_axis = 1;
    } else if (!c_type3.empty() &&
               (c_type3.contains("FELO") || c_type3.contains("FREQ") || c_type3.contains("VELO") || c_type3.contains("VOPT") ||
                   c_type3.contains("VRAD") || c_type3.contains("WAVE") || c_type3.contains("AWAV"))) {
        chan_axis = 2;
    } else if (!c_type4.empty() &&
               (c_type4.contains("FELO") || c_type4.contains("FREQ") || c_type4.contains("VELO") || c_type4.contains("VOPT") ||
                   c_type4.contains("VRAD") || c_type4.contains("WAVE") || c_type4.contains("AWAV"))) {
        chan_axis = 3;
    } else {
        chan_axis = -1;
    }

    // find stokes axis
    if (c_type1 == "STOKES")
        stokes_axis = 0;
    else if (c_type2 == "STOKES")
        stokes_axis = 1;
    else if (c_type3 == "STOKES")
        stokes_axis = 2;
    else if (c_type4 == "STOKES")
        stokes_axis = 3;
    else
        stokes_axis = -1;
}

// ***** FITS keyword conversion *****

void FileInfoLoader::ConvertAxisName(std::string& axis_name, std::string& projection, casacore::MDirection::Types type) {
    // Convert direction axis name to FITS name and append projection
    if ((axis_name == "Right Ascension") || (axis_name == "Hour Angle")) {
        axis_name = (projection.empty() ? "RA" : "RA---" + projection);
    } else if (axis_name == "Declination") {
        axis_name = (projection.empty() ? "DEC" : "DEC--" + projection);
    } else if (axis_name == "Longitude") {
        switch (type) {
            case casacore::MDirection::GALACTIC:
                axis_name = (projection.empty() ? "GLON" : "GLON-" + projection);
                break;
            case casacore::MDirection::SUPERGAL:
                axis_name = (projection.empty() ? "SLON" : "SLON-" + projection);
                break;
            case casacore::MDirection::ECLIPTIC:
            case casacore::MDirection::MECLIPTIC:
            case casacore::MDirection::TECLIPTIC:
                axis_name = (projection.empty() ? "ELON" : "ELON-" + projection);
                break;
            default:
                break;
        }
    } else if (axis_name == "Latitude") {
        switch (type) {
            case casacore::MDirection::GALACTIC:
                axis_name = (projection.empty() ? "GLAT" : "GLAT-" + projection);
                break;
            case casacore::MDirection::SUPERGAL:
                axis_name = (projection.empty() ? "SLAT" : "SLAT-" + projection);
                break;
            case casacore::MDirection::ECLIPTIC:
            case casacore::MDirection::MECLIPTIC:
            case casacore::MDirection::TECLIPTIC:
                axis_name = (projection.empty() ? "ELAT" : "ELAT-" + projection);
                break;
            default:
                break;
        }
    }
}

void FileInfoLoader::MakeRadeSysStr(std::string& rade_sys, const std::string& equinox) {
    // append equinox to radesys
    std::string prefix;
    if (!equinox.empty()) {
        if ((rade_sys.compare("FK4") == 0) && (equinox[0] != 'B'))
            prefix = "B";
        else if ((rade_sys.compare("FK5") == 0) && (equinox[0] != 'J'))
            prefix = "J";
    }
    if (!rade_sys.empty() && !equinox.empty())
        rade_sys.append(", ");
    rade_sys.append(prefix + equinox);
}

void FileInfoLoader::GetRadeSysFromEquinox(std::string& rade_sys, std::string& equinox) {
    // according to casacore::FITSCoordinateUtil::toFITSHeader
    if (equinox.find("ICRS") != std::string::npos) {
        rade_sys = "ICRS";
        equinox = "2000";
    } else if (equinox.find("2000") != std::string::npos) {
        rade_sys = "FK5";
    } else if (equinox.find("B1950") != std::string::npos) {
        rade_sys = "FK4";
    }
}

std::string FileInfoLoader::MakeValueStr(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    std::string val_str;
    if (unit.empty()) {
        val_str = fmt::format("{} {}", val, unit);
    } else {
        // convert to uppercase for string comparisons
        std::string upper_type(type);
        transform(upper_type.begin(), upper_type.end(), upper_type.begin(), ::toupper);
        bool is_ra(upper_type.find("RA") != std::string::npos);
        bool is_angle((upper_type.find("DEC") != std::string::npos) || (upper_type.find("LON") != std::string::npos) ||
                      (upper_type.find("LAT") != std::string::npos));
        if (is_ra || is_angle) {
            casacore::MVAngle::formatTypes format(casacore::MVAngle::ANGLE);
            if (is_ra) {
                format = casacore::MVAngle::TIME;
            }
            casacore::Quantity quant1(val, unit);
            casacore::MVAngle mva(quant1);
            val_str = mva.string(format, 10);
        } else {
            val_str = fmt::format("{} {}", val, unit);
        }
    }
    return val_str;
}

bool FileInfoLoader::ConvertSpecSysToFits(std::string& spec_sys, casacore::MFrequency::Types type) {
    // use labels in FITS headers
    bool result(true);
    switch (type) {
        case casacore::MFrequency::LSRK:
            spec_sys = "LSRK";
            break;
        case casacore::MFrequency::BARY:
            spec_sys = "BARYCENT";
            break;
        case casacore::MFrequency::LSRD:
            spec_sys = "LSRD";
            break;
        case casacore::MFrequency::GEO:
            spec_sys = "GEOCENTR";
            break;
        case casacore::MFrequency::REST:
            spec_sys = "SOURCE";
            break;
        case casacore::MFrequency::GALACTO:
            spec_sys = "GALACTOC";
            break;
        case casacore::MFrequency::LGROUP:
            spec_sys = "LOCALGRP";
            break;
        case casacore::MFrequency::CMB:
            spec_sys = "CMBDIPOL";
            break;
        case casacore::MFrequency::TOPO:
            spec_sys = "TOPOCENT";
            break;
        default:
            spec_sys = "";
            result = false;
    }
    return result;
}

// ***** Unit conversion *****

std::string FileInfoLoader::UnitConversion(const double value, const std::string& unit) {
    if (std::regex_match(unit, std::regex("rad", std::regex::icase))) {
        return Deg2Arcsec(Rad2Deg(value));
    } else if (std::regex_match(unit, std::regex("deg", std::regex::icase))) {
        return Deg2Arcsec(value);
    } else if (std::regex_match(unit, std::regex("hz", std::regex::icase))) {
        return ConvertHz(value);
    } else if (std::regex_match(unit, std::regex("arcsec", std::regex::icase))) {
        return fmt::format("{:.3f}\"", value);
    } else { // unknown
        return fmt::format("{:.3f} {}", value, unit);
    }
}

// Unit conversion: convert radians to degree
double FileInfoLoader::Rad2Deg(const double rad) {
    return 57.29577951 * rad;
}

// Unit conversion: convert degree to arcsec
std::string FileInfoLoader::Deg2Arcsec(const double degree) {
    // 1 degree = 60 arcmin = 60*60 arcsec
    double arcs = fabs(degree * 3600);

    // customized format of arcsec
    if (arcs >= 60.0) { // arcs >= 60, convert to arcmin
        return fmt::format("{:.2f}\'", degree < 0 ? -1 * arcs / 60 : arcs / 60);
    } else if (arcs < 60.0 && arcs > 0.1) { // 0.1 < arcs < 60
        return fmt::format("{:.2f}\"", degree < 0 ? -1 * arcs : arcs);
    } else if (arcs <= 0.1 && arcs > 0.01) { // 0.01 < arcs <= 0.1
        return fmt::format("{:.3f}\"", degree < 0 ? -1 * arcs : arcs);
    } else { // arcs <= 0.01
        return fmt::format("{:.4f}\"", degree < 0 ? -1 * arcs : arcs);
    }
}

// Unit conversion: convert Hz to MHz or GHz
std::string FileInfoLoader::ConvertHz(const double hz) {
    if (hz >= 1.0e9) {
        return fmt::format("{:.4f} GHz", hz / 1.0e9);
    } else if (hz < 1.0e9 && hz >= 1.0e6) {
        return fmt::format("{:.4f} MHz", hz / 1.0e6);
    } else {
        return fmt::format("{:.4f} Hz", hz);
    }
}
