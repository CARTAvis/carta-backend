#ifndef CARTA_BACKEND_FILELIST_PAGEDINFOLOADER_H_
#define CARTA_BACKEND_FILELIST_PAGEDINFOLOADER_H_

#include "FileInfoLoader.h"

#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>

class PagedInfoLoader : public FileInfoLoader {
public:
    PagedInfoLoader(const std::string& filename, bool is_casa);

protected:
    virtual CARTA::FileType GetCartaFileType() override;
    virtual bool FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) override;

private:
    void ConvertAxisName(std::string& axis_name, std::string& projection, casacore::MDirection::Types type);
    bool ConvertSpecSysToFits(std::string& spec_sys, casacore::MFrequency::Types type);
    void GetRadeSysFromEquinox(std::string& rade_sys, std::string& equinox);

    CARTA::FileType _image_type;
};

PagedInfoLoader::PagedInfoLoader(const std::string& filename, bool is_casa) {
    _filename = filename;
    _image_type = (is_casa ? CARTA::FileType::CASA : CARTA::FileType::MIRIAD);
}

CARTA::FileType PagedInfoLoader::GetCartaFileType() {
    return _image_type;
}

bool PagedInfoLoader::FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    bool ext_info_ok(true);
    casacore::ImageInterface<casacore::Float>* cc_image(nullptr);
    try {
        switch (_image_type) {
            case CARTA::FileType::CASA: {
                cc_image = new casacore::PagedImage<casacore::Float>(_filename);
                break;
            }
            case CARTA::FileType::MIRIAD: {
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

void PagedInfoLoader::ConvertAxisName(std::string& axis_name, std::string& projection, casacore::MDirection::Types type) {
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

bool PagedInfoLoader::ConvertSpecSysToFits(std::string& spec_sys, casacore::MFrequency::Types type) {
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

void PagedInfoLoader::GetRadeSysFromEquinox(std::string& rade_sys, std::string& equinox) {
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

#endif // CARTA_BACKEND_FILELIST_PAGEDINFOLOADER_H_
