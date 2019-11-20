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

#include "FitsInfoLoader.h"
#include "GenericInfoLoader.h"
#include "Hdf5InfoLoader.h"
#include "PagedInfoLoader.h"

//#################################################################################
// FILE INFO LOADER

FileInfoLoader* FileInfoLoader::GetInfoLoader(const std::string& filename) {
    casacore::ImageOpener::ImageTypes image_type = casacore::ImageOpener::imageType(filename);
    switch (image_type) {
        case casacore::ImageOpener::AIPSPP:
            return new PagedInfoLoader(filename, true);
        case casacore::ImageOpener::FITS:
            return new FitsInfoLoader(filename);
        case casacore::ImageOpener::MIRIAD:
            return new PagedInfoLoader(filename, false);
        case casacore::ImageOpener::GIPSY:
            break;
        case casacore::ImageOpener::CAIPS:
            break;
        case casacore::ImageOpener::NEWSTAR:
            break;
        case casacore::ImageOpener::HDF5:
            return new Hdf5InfoLoader(filename);
        case casacore::ImageOpener::IMAGECONCAT:
            break;
        case casacore::ImageOpener::IMAGEEXPR:
            break;
        case casacore::ImageOpener::COMPLISTIMAGE:
            break;
        default:
            break;
    }
    return new GenericInfoLoader(filename);
}

//#################################################################################
// FILE INFO

bool FileInfoLoader::FillFileInfo(CARTA::FileInfo* file_info) {
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }
    std::string filename_only = cc_file.path().baseName();
    file_info->set_name(filename_only);

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
    CARTA::FileType file_type(GetCartaFileType());
    file_info->set_type(file_type);
    casacore::String abs_file_name(cc_file.path().absoluteName());
    return GetHduList(file_info, abs_file_name);
}

bool FileInfoLoader::GetHduList(CARTA::FileInfo* file_info, const std::string& filename) {
    // fill FileInfo hdu list
    file_info->add_hdu_list("");
    return true;
}

//#################################################################################
// FILE INFO EXTENDED

bool FileInfoLoader::FillFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }

    std::string name(cc_file.path().baseName());
    auto entry = ext_info->add_computed_entries();
    entry->set_name("Name");
    entry->set_value(name);
    entry->set_entry_type(CARTA::EntryType::STRING);
    return FillExtFileInfo(ext_info, hdu, message);
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
    chan_axis = -1;
    stokes_axis = -1;

    // Note header axes are 1-based but shape is 0-based
    casacore::String c_type1(axis_type_1), c_type2(axis_type_2), c_type3(axis_type_3), c_type4(axis_type_4);
    // uppercase for string comparisons
    c_type1.upcase();
    c_type2.upcase();
    c_type3.upcase();
    c_type4.upcase();

    size_t ntypes(4);
    const casacore::String ctypes[] = {c_type1, c_type2, c_type3, c_type4};
    const casacore::String spectral_types[] = {"FELO", "FREQ", "VELO", "VOPT", "VRAD", "WAVE", "AWAV"};
    const casacore::String stokes_type = "STOKES";
    for (size_t i = 0; i < ntypes; ++i) {
        for (auto& spectral_type : spectral_types) {
            if (ctypes[i].contains(spectral_type)) {
                chan_axis = i;
            }
        }
        if (ctypes[i] == stokes_type) {
            stokes_axis = i;
        }
    }
}

// ***** FITS keyword conversion *****

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
