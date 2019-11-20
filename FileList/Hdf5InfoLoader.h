#ifndef CARTA_BACKEND_FILELIST_HDF5INFOLOADER_H_
#define CARTA_BACKEND_FILELIST_HDF5INFOLOADER_H_

#include "../ImageData/Hdf5Attributes.h"
#include "FileInfoLoader.h"

class Hdf5InfoLoader : public FileInfoLoader {
public:
    Hdf5InfoLoader(const std::string& filename);

protected:
    CARTA::FileType GetCartaFileType() override;
    virtual bool GetHduList(CARTA::FileInfo* file_info, const std::string& abs_filename) override;
    virtual bool FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) override;

private:
    std::string GetStringAttribute(casacore::Record& record, std::string field);
    double GetDoubleAttribute(casacore::Record& record, std::string field);
};

Hdf5InfoLoader::Hdf5InfoLoader(const std::string& filename) {
    _filename = filename;
}

CARTA::FileType Hdf5InfoLoader::GetCartaFileType() {
    return CARTA::FileType::HDF5;
}

bool Hdf5InfoLoader::GetHduList(CARTA::FileInfo* file_info, const std::string& abs_filename) {
    casacore::HDF5File hdf_file(abs_filename);
    std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdf_file));
    if (hdus.empty()) {
        file_info->add_hdu_list("");
    } else {
        for (auto group_name : hdus) {
            file_info->add_hdu_list(group_name);
        }
    }
    return true;
}

bool Hdf5InfoLoader::FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    // Add extended info for HDF5 file
    try {
        auto hdf_file_ptr = casacore::CountedPtr<casacore::HDF5File>(new casacore::HDF5File(_filename));

        if (hdu.empty()) { // use first
            hdu = casacore::HDF5Group::linkNames(*hdf_file_ptr)[0];
        }
        casacore::HDF5Group hdf_group(*hdf_file_ptr, hdu, true);
        casacore::Record attributes;
        try {
            // read attributes into casacore Record
            attributes = Hdf5Attributes::ReadAttributes(hdf_group.getHid());
        } catch (casacore::HDF5Error& err) {
            message = "Error reading HDF5 attributes: " + err.getMesg();
            return false;
        }

        if (attributes.empty()) {
            message = "No HDF5 attributes";
            return false;
        }

        // check dimensions
        casacore::uInt num_dims;
        casacore::IPosition data_shape;

        try {
            casacore::HDF5Lattice<float> hdf5_lattice = casacore::HDF5Lattice<float>(hdf_file_ptr, "DATA", hdu);
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

std::string Hdf5InfoLoader::GetStringAttribute(casacore::Record& record, std::string field) {
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

double Hdf5InfoLoader::GetDoubleAttribute(casacore::Record& record, std::string field) {
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

#endif // CARTA_BACKEND_FILELIST_HDF5INFOLOADER_H_
