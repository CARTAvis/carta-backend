/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileExtInfoLoader.h"

#include <fitsio.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>

#include <casacore/casa/Quanta/MVAngle.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/images/Images/FITSImage.h>
#include <casacore/images/Images/ImageFITSConverter.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>

#include "../ImageData/CartaFitsImage.h"
#include "../ImageData/CartaHdf5Image.h"
#include "FileList/FitsHduList.h"
#include "Logger/Logger.h"
#include "Util/Casacore.h"
#include "Util/File.h"
#include "Util/FileSystem.h"
#include "Util/Image.h"

using namespace carta;

FileExtInfoLoader::FileExtInfoLoader(std::shared_ptr<FileLoader> loader) : _loader(loader) {}

bool FileExtInfoLoader::FillFitsFileInfoMap(
    std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map, const std::string& filename, std::string& message) {
    // Fill map with FileInfoExtended for all FITS image HDUs
    bool map_ok(false);

    if (IsCompressedFits(filename)) {
        CompressedFits cfits(filename);
        if (!cfits.GetFitsHeaderInfo(hdu_info_map)) {
            message = "Compressed FITS headers failed.";
            return map_ok;
        }

        for (auto& hdu_info : hdu_info_map) {
            std::vector<int> render_axes = {0, 1}; // default
            AddInitialComputedEntries(hdu_info.first, hdu_info.second, filename, render_axes, &cfits);

            // Use headers in FileInfoExtended to create computed entries
            int spectral_axis = cfits.GetSpectralAxis();
            int stokes_axis = cfits.GetStokesAxis();
            AddComputedEntriesFromHeaders(hdu_info.second, render_axes, spectral_axis, stokes_axis, &cfits);

            const casacore::ImageBeamSet beam_set = cfits.GetBeamSet();
            if (!beam_set.empty()) {
                AddBeamEntry(hdu_info.second, beam_set);
            }
        }
    } else {
        // Get list of image HDUs
        std::vector<std::string> hdu_list;
        FitsHduList fits_hdu_list = FitsHduList(filename);
        fits_hdu_list.GetHduList(hdu_list, message);

        if (hdu_list.empty()) {
            message = "No image HDUs found.";
            return map_ok;
        }

        // Get FileInfoExtended for each hdu
        for (auto& hdu : hdu_list) {
            std::string hdu_num(hdu);
            StripHduName(hdu_num);

            CARTA::FileInfoExtended file_info_ext;
            if (FillFileExtInfo(file_info_ext, filename, hdu_num, message)) {
                hdu_info_map[hdu_num] = file_info_ext;
            }
        }
    }

    map_ok = !hdu_info_map.empty();
    if (!map_ok) {
        message = "Error loading headers or image.";
    }

    return map_ok;
}

bool FileExtInfoLoader::FillFileExtInfo(
    CARTA::FileInfoExtended& extended_info, const std::string& filename, const std::string& hdu, std::string& message) {
    // Fill FileInfoExtended for specific hdu
    // Set name from filename
    fs::path filepath(filename);
    std::string filename_nopath = filepath.filename().string();
    auto entry = extended_info.add_computed_entries();
    entry->set_name("Name");
    entry->set_value(filename_nopath);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // Fill header_entries, computed_entries
    bool info_ok(false);
    if (_loader && _loader->CanOpenFile(message)) {
        info_ok = FillFileInfoFromImage(extended_info, hdu, message);
    }

    bool has_mips = _loader->HasMip(2);
    if (has_mips) {
        auto has_mip_entry = extended_info.add_computed_entries();
        has_mip_entry->set_name("Has mipmaps");
        has_mip_entry->set_value("T");
        has_mip_entry->set_entry_type(CARTA::EntryType::STRING);
    }

    return info_ok;
}

void FileExtInfoLoader::StripHduName(std::string& hdu) {
    // Strip extension name if any from hdu_name
    if (!hdu.empty()) {
        // split hdu_name number and ext name (if any)
        size_t delim_pos = hdu.find(":");
        if (delim_pos != std::string::npos) {
            hdu = hdu.substr(0, delim_pos);
        }
    }
}

bool FileExtInfoLoader::FillFileInfoFromImage(CARTA::FileInfoExtended& extended_info, const std::string& hdu, std::string& message) {
    // add header_entries in FITS format (issue #13) using ImageInterface from FileLoader
    bool info_ok(false);
    if (_loader) {
        try {
            _loader->OpenFile(hdu);

            bool check_data_type(false);
            auto image = _loader->GetImage(check_data_type);

            if (image) {
                // Check dimensions
                casacore::IPosition image_shape(image->shape());
                unsigned int num_dim = image_shape.size();

                if (num_dim < 2 || num_dim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return info_ok;
                }

                // For computed entries:
                auto data_type = _loader->GetDataType();
                auto equivalent_type = data_type; // for FITS only, for rescaled data
                casacore::String image_type(image->imageType());
                bool use_image_for_entries(false);

                if (image_type == "FITSImage") {
                    // casacore FitsKeywordList has incomplete header names (no n on CRVALn, CDELTn, CROTA, etc.) so read with fitsio
                    casacore::FITSImage* fits_image = dynamic_cast<casacore::FITSImage*>(image.get());
                    casacore::String filename(fits_image->name());
                    equivalent_type = fits_image->dataType();
                    casacore::Vector<casacore::String> headers = FitsHeaderStrings(filename, FileInfo::GetFitsHdu(hdu));

                    if (headers.empty()) {
                        // Found unsupported headers
                        // Get image headers in FITS format using casacore ImageHeaderToFITS
                        casacore::ImageFITSHeaderInfo fhi;
                        casacore::String error_string;
                        if (GetFITSHeader(image, hdu, fhi, error_string)) {
                            // Set header entries from ImageFITSHeaderInfo
                            FitsHeaderInfoToHeaderEntries(fhi, extended_info);
                            use_image_for_entries = true;
                        } else {
                            message = error_string;
                            return false;
                        }
                        if (!hdu.empty()) {
                            auto entry = extended_info.add_computed_entries();
                            entry->set_name("HDU");
                            entry->set_value(hdu);
                            entry->set_entry_type(CARTA::EntryType::STRING);
                        }
                    } else {
                        AddEntriesFromHeaderStrings(headers, hdu, extended_info);
                    }
                } else if (image_type == "CartaFitsImage") {
                    CartaFitsImage* fits_image = dynamic_cast<CartaFitsImage*>(image.get());
                    equivalent_type = fits_image->dataType();
                    casacore::Vector<casacore::String> headers = fits_image->FitsHeaderStrings();
                    AddEntriesFromHeaderStrings(headers, hdu, extended_info);
                } else if (image_type == "CartaHdf5Image") {
                    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(image.get());
                    casacore::Vector<casacore::String> headers = hdf5_image->FitsHeaderStrings();
                    AddEntriesFromHeaderStrings(headers, hdu, extended_info);
                } else {
                    // Get image headers in FITS format using casacore ImageHeaderToFITS
                    casacore::ImageFITSHeaderInfo fhi;
                    casacore::String error_string;
                    if (GetFITSHeader(image, hdu, fhi, error_string)) {
                        // Set header entries from ImageFITSHeaderInfo
                        FitsHeaderInfoToHeaderEntries(fhi, extended_info);
                        use_image_for_entries = true;
                    } else {
                        message = error_string;
                        return false;
                    }
                }

                AddDataTypeEntry(extended_info, data_type, equivalent_type);

                std::vector<int> spatial_axes, render_axes;
                int spectral_axis, stokes_axis, depth_axis;
                if (_loader->FindCoordinateAxes(image_shape, spatial_axes, spectral_axis, stokes_axis, render_axes, depth_axis, message)) {
                    casacore::Vector<casacore::String> axes_names;
                    AddShapeEntries(
                        extended_info, image_shape, spatial_axes, spectral_axis, stokes_axis, render_axes, depth_axis, axes_names);

                    // Computed entries for rendered image axes, depth axis (may not be spectral), stokes axis
                    AddComputedEntries(extended_info, image.get(), render_axes, spectral_axis, stokes_axis, use_image_for_entries);
                    info_ok = true;
                }
            } else { // image failed
                message = "Image could not be opened.";
            }

            _loader->CloseImageIfUpdated();
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
            spdlog::debug("Extended file info exception: {}", message);
            if (message.find("diagonal") != std::string::npos) { // "ArrayBase::diagonal() - diagonal out of range"
                message = "Failed to open image at specified HDU.";
            } else if (message.find("No image at specified location") != std::string::npos) {
                message = "No image at specified HDU.";
            }
        }
    } else { // loader failed
        message = "Image type not supported.";
    }

    return info_ok;
}

void FileExtInfoLoader::AddEntriesFromHeaderStrings(
    const casacore::Vector<casacore::String>& headers, const std::string& hdu, CARTA::FileInfoExtended& extended_info) {
    // Parse each header in vector and add header entry to FileInfoExtended
    casacore::String extname, stokes_ctype_num; // used to set stokes values in loader

    // CartaHdf5Image shortens keywords to FITS length 8
    std::unordered_map<std::string, std::string> hdf5_keys = {
        {"H5SCHEMA", "SCHEMA_VERSION"}, {"H5CNVRTR", "HDF5_CONVERTER"}, {"H5CONVSN", "HDF5_CONVERTER_VERSION"}, {"H5DATE", "HDF5_DATE"}};

    for (auto& header : headers) {
        // Parse header into name, value, comment
        casacore::String name(header);
        name.trim();

        if (name.empty()) {
            continue;
        }

        if (name == "END") {
            break;
        }

        if (name.startsWith("HISTORY") || name.startsWith("COMMENT")) {
            auto entry = extended_info.add_header_entries();
            entry->set_name(name);
            continue;
        }

        // Find '=' for name = value format
        auto eq_pos = header.find('=', 0);
        name = header.substr(0, eq_pos);
        name.trim();

        // Flag whether to convert value to numeric
        bool string_value(false);
        casacore::String value, comment;

        if (eq_pos != std::string::npos) {
            // Set remainder to everything after =
            casacore::String remainder = header.substr(eq_pos + 1);
            remainder.trim();

            // Check if value quoted (may contain /), then find comment after /
            size_t slash_pos(std::string::npos);
            if ((remainder[0] == '\'') || (remainder[0] == '"')) {
                auto end_quote = remainder.find(remainder[0], 1);

                // set value between quotes
                value = remainder.substr(1, end_quote - 1);
                value.trim();
                string_value = true; // do not convert to numeric

                // set beginning of comment
                slash_pos = remainder.find('/', end_quote + 1);
            } else {
                // set beginning of comment
                slash_pos = remainder.find('/', 0);

                // set value before comment
                value = remainder.substr(0, slash_pos - 1);
                value.trim();
            }

            if (slash_pos != std::string::npos) {
                comment = remainder.substr(slash_pos + 1);
                comment.trim();
            }
        }

        if (hdf5_keys.count(name)) {
            name = hdf5_keys[name];
        }

        if (name.startsWith("CTYPE") && ((value == "STOKES") || (value == "Stokes") || (value == "stokes"))) {
            stokes_ctype_num = name.back();
        } else if (name == "EXTNAME") {
            extname = value;
        } else if (name == "SIMPLE") {
            try {
                // In some images, this is numeric value
                int numval = std::stoi(value);
                value = (numval ? "T" : "F");
                string_value = true;
            } catch (std::invalid_argument& err) {
                // not numeric
            }
        }

        // Set name
        auto entry = extended_info.add_header_entries();
        entry->set_name(name);

        if (!value.empty()) {
            // Set numeric value
            if (string_value) {
                *entry->mutable_value() = value;
            } else {
                ConvertHeaderValueToNumeric(name, value, entry);
            }

            // Set numeric values for stokes axis in loader
            if (name == ("CRVAL" + stokes_ctype_num)) {
                _loader->SetStokesCrval((float)entry->numeric_value());
            } else if (name == ("CRPIX" + stokes_ctype_num)) {
                _loader->SetStokesCrpix((float)entry->numeric_value());
            } else if (name == ("CDELT" + stokes_ctype_num)) {
                _loader->SetStokesCdelt((int)entry->numeric_value());
            }
        }

        if (!comment.empty()) {
            // Set comment
            entry->set_comment(comment);
        }
    }

    // Create FileInfoExtended computed_entries for hdu, extension name
    if (!hdu.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("HDU");
        entry->set_value(hdu);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!extname.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Extension name");
        entry->set_value(extname);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }
}

void FileExtInfoLoader::ConvertHeaderValueToNumeric(const casacore::String& name, casacore::String& value, CARTA::HeaderEntry* entry) {
    // Convert string value to double, float, or int.  Set as string type if conversion fails.
    if (value.contains(".")) {
        // Float or double type?
        try {
            double dvalue = std::stod(value);

            std::string string_value;
            if (name.contains("PIX") || (name == "EQUINOX") || (name == "EPOCH")) {
                string_value = fmt::format("{}", dvalue);
            } else {
                string_value = fmt::format("{:.12E}", dvalue);
            }
            *entry->mutable_value() = string_value;
            entry->set_numeric_value(dvalue);
            entry->set_entry_type(CARTA::EntryType::FLOAT);
        } catch (std::invalid_argument) {
            // Not a number - set string value only
            *entry->mutable_value() = value;
            entry->set_entry_type(CARTA::EntryType::STRING);
        } catch (std::out_of_range) {
            try {
                char* endptr(nullptr);
                long double ldvalue = std::strtold(value.c_str(), &endptr);

                std::string string_value = fmt::format("{:.12E}", ldvalue);
                *entry->mutable_value() = string_value;
                entry->set_numeric_value(ldvalue);
                entry->set_entry_type(CARTA::EntryType::FLOAT);
            } catch (std::out_of_range) {
                *entry->mutable_value() = value;
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }
    } else {
        // Int type?
        try {
            int ivalue = std::stoi(value);
            std::string string_value = fmt::format("{:d}", ivalue);

            *entry->mutable_value() = value;
            entry->set_numeric_value(ivalue);
            entry->set_entry_type(CARTA::EntryType::INT);
        } catch (std::invalid_argument) {
            // Not a number - set string value only
            *entry->mutable_value() = value;
            entry->set_entry_type(CARTA::EntryType::STRING);
        } catch (std::out_of_range) {
            try {
                // long numeric value
                long lvalue = std::stol(value);

                std::string string_value = fmt::format("{:d}", lvalue);
                *entry->mutable_value() = string_value;
                entry->set_numeric_value(lvalue);
                entry->set_entry_type(CARTA::EntryType::INT);
            } catch (std::out_of_range) {
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }
    }
}

void FileExtInfoLoader::FitsHeaderInfoToHeaderEntries(casacore::ImageFITSHeaderInfo& fhi, CARTA::FileInfoExtended& extended_info) {
    // Fill FileInfoExtended header_entries from ImageFITSHeaderInfo and begin computed_entries.
    // Axis or coord number to append to name
    int naxis(0), ntype(1), nval(1), ndelt(1), npix(1);

    casacore::String stokes_axis_num; // stokes values for loader

    // Create FileInfoExtended header_entries for each FitsKeyword
    fhi.kw.first(); // go to first card
    casacore::FitsKeyword* fkw = fhi.kw.next();

    while (fkw) {
        casacore::String name(fkw->name());

        // Do not include ORIGIN (casacore), DATE (current) added by ImageHeaderToFITS, or END
        if ((name == "DATE") || (name == "ORIGIN") || (name == "END")) {
            fkw = fhi.kw.next(); // get next keyword
            continue;
        }

        // Strangely, FitsKeyword does not append axis/coord number so do it here
        if ((name == "NAXIS")) {
            if (naxis > 0) {
                name += casacore::String::toString(naxis++);
            } else {
                naxis++;
            }
        }

        // Modify names: append number or use longer string
        if (name == "CTYPE") { // append type number
            name += casacore::String::toString(ntype++);
        } else if (name == "CRVAL") { // append val number
            name += casacore::String::toString(nval++);
        } else if (name == "CDELT") { // append delt number
            name += casacore::String::toString(ndelt++);
        } else if (name == "CRPIX") { // append pix number
            name += casacore::String::toString(npix++);
        }

        // Fill the first stokes type and the delta value for the stokes index. Set the first stokes type index as 0
        if (!stokes_axis_num.empty()) {
            if (name == ("CRVAL" + stokes_axis_num)) {
                _loader->SetStokesCrval((float)fkw->asDouble());
            } else if (name == ("CRPIX" + stokes_axis_num)) {
                _loader->SetStokesCrpix((float)fkw->asDouble());
            } else if (name == ("CDELT" + stokes_axis_num)) {
                _loader->SetStokesCdelt((int)fkw->asDouble());
            }
        }

        // Fill HeaderEntry fields
        auto header_entry = extended_info.add_header_entries();
        header_entry->set_name(name);
        header_entry->set_comment(fkw->comm());

        switch (fkw->type()) {
            case casacore::FITS::NOVALUE:
                break;
            case casacore::FITS::LOGICAL: {
                bool value(fkw->asBool());
                std::string bool_string(value ? "T" : "F");

                *header_entry->mutable_value() = bool_string;
                header_entry->set_entry_type(CARTA::EntryType::INT);
                header_entry->set_numeric_value(value);
                break;
            }
            case casacore::FITS::SHORT:
            case casacore::FITS::LONG: {
                int value(fkw->asInt());
                std::string string_value = fmt::format("{:d}", value);

                *header_entry->mutable_value() = string_value;
                header_entry->set_entry_type(CARTA::EntryType::INT);
                header_entry->set_numeric_value(value);
                break;
            }
            case casacore::FITS::FLOAT:
            case casacore::FITS::DOUBLE:
            case casacore::FITS::REAL: {
                double value(fkw->asDouble());
                std::string string_value;
                if ((name.find("PIX") != std::string::npos) || (name.find("EQUINOX") != std::string::npos) ||
                    (name.find("EPOCH") != std::string::npos)) {
                    string_value = fmt::format("{}", value);
                } else {
                    string_value = fmt::format("{:.12E}", value);
                }
                *header_entry->mutable_value() = string_value;
                header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                header_entry->set_numeric_value(value);
                break;
            }
            case casacore::FITS::STRING:
            case casacore::FITS::FSTRING: {
                casacore::String header_string = fkw->asString();
                header_string.trim();

                if (name.contains("CTYPE")) {
                    if (header_string.startsWith("FREQ")) {
                        // Fix header with "FREQUENCY"
                        header_string = "FREQ";
                    } else if (header_string.startsWith("STOKES") || header_string.startsWith("Stokes") ||
                               header_string.startsWith("stokes")) {
                        // Found CTYPEX = STOKES; set the stokes axis number X
                        stokes_axis_num = name.back();
                    }
                }

                *header_entry->mutable_value() = header_string;
                header_entry->set_entry_type(CARTA::EntryType::STRING);
                break;
            }
            case casacore::FITS::COMPLEX: {
                casacore::Complex value = fkw->asComplex();
                std::string string_value = fmt::format("{} + {}i", value.real(), value.imag());

                *header_entry->mutable_value() = string_value;
                header_entry->set_entry_type(CARTA::EntryType::STRING);
                break;
            }
            case casacore::FITS::ICOMPLEX: {
                casacore::IComplex value = fkw->asIComplex();
                std::string string_value = fmt::format("{} + {}i", value.real(), value.imag());

                *header_entry->mutable_value() = string_value;
                header_entry->set_entry_type(CARTA::EntryType::STRING);
                break;
            }
            case casacore::FITS::DCOMPLEX: {
                casacore::DComplex value = fkw->asDComplex();
                std::string string_value = fmt::format("{} + {}i", value.real(), value.imag());

                *header_entry->mutable_value() = string_value;
                header_entry->set_entry_type(CARTA::EntryType::STRING);
                break;
            }
            default: {
                casacore::String header_string = fkw->asString();
                header_string.trim();

                *header_entry->mutable_value() = header_string;
                header_entry->set_entry_type(CARTA::EntryType::STRING);
                break;
            }
        }

        fkw = fhi.kw.next(); // get next keyword
    }
}

// ***** Computed entries *****

void FileExtInfoLoader::AddInitialComputedEntries(const std::string& hdu, CARTA::FileInfoExtended& extended_info,
    const std::string& filename, const std::vector<int>& render_axes, CompressedFits* compressed_fits) {
    // Add computed entries for filename, hdu, data type, shape, and axes
    fs::path filepath(filename);
    std::string filename_nopath = filepath.filename().string();
    auto entry = extended_info.add_computed_entries();
    entry->set_name("Name");
    entry->set_value(filename_nopath);
    entry->set_entry_type(CARTA::EntryType::STRING);

    if (!hdu.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("HDU");
        entry->set_value(hdu);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    // Describe data type, shape, and axes
    int bitpix(0);
    double bscale(1.0), bzero(0.0);
    casacore::IPosition shape;
    std::vector<int> spatial_axes(2, -1);
    int spectral_axis(-1), stokes_axis(-1), depth_axis(-1);
    casacore::Vector<casacore::String> axes_names(4, "NA");
    std::vector<std::string> spectral_ctypes = {"FREQ", "WAV", "ENER", "VOPT", "ZOPT", "VELO", "VRAD", "BETA", "FELO"};

    for (int i = 0; i < extended_info.header_entries_size(); ++i) {
        auto header_entry = extended_info.header_entries(i);
        auto entry_name = header_entry.name();

        if (entry_name == "EXTNAME") {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Extension name");
            entry->set_value(header_entry.value());
            entry->set_entry_type(CARTA::EntryType::STRING);
        } else if (entry_name.find("NAXIS") == 0) {
            std::string naxis_index(&entry_name.back());
            auto entry_num_value = header_entry.numeric_value();

            if (naxis_index == "S") { // just "NAXIS"
                shape.resize(entry_num_value);
            } else { // NAXISn (1-based)
                int n = stoi(naxis_index);
                shape(n - 1) = entry_num_value;
            }
        } else if (entry_name.find("CTYPE") == 0) {
            std::string ctype_index(&entry_name.back());
            int axis_num = stoi(ctype_index) - 1;
            auto entry_value = header_entry.value();

            if (!entry_value.empty()) {
                entry_value = entry_value.substr(0, entry_value.find("-", 0));
                std::transform(
                    entry_value.begin(), entry_value.end(), entry_value.begin(), [](unsigned char c) { return std::toupper(c); });

                // Fill in axis names
                if (axis_num >= 0 && axis_num < axes_names.size()) {
                    axes_names[axis_num] = entry_value;
                }

                // Assign axis numbers for different types
                if (entry_value.find("RA") == 0 || entry_value.find("GLON") == 0 || entry_value.find("UU") == 0) {
                    spatial_axes[0] = axis_num;
                } else if (entry_value.find("DEC") == 0 || entry_value.find("GLAT") == 0 || entry_value.find("VV") == 0) {
                    spatial_axes[1] = axis_num;
                } else if (entry_value.find("STOKES") == 0) {
                    stokes_axis = axis_num;
                } else if (std::any_of(spectral_ctypes.begin(), spectral_ctypes.end(),
                               [&](const std::string& key_word) { return (entry_value.find(key_word) != std::string::npos); })) {
                    spectral_axis = axis_num;
                }
            }

            // Depth axis is not the first two axes [0, 1], i.e., non-render axis that is not stokes (if any)
            if (axis_num > 1 && axis_num != stokes_axis) {
                depth_axis = axis_num;
            }
        } else if (entry_name.find("BITPIX") == 0) {
            bitpix = header_entry.numeric_value();
        } else if (entry_name.find("BSCALE") == 0) {
            bscale = header_entry.numeric_value();
        } else if (entry_name.find("BZERO") == 0) {
            bzero = header_entry.numeric_value();
        }
    }

    // Add data type from BITPIX header
    casacore::DataType data_type(casacore::DataType::TpOther);
    if (bitpix_types.find(bitpix) != bitpix_types.end()) {
        data_type = bitpix_types[bitpix];
    }
    // Check if data type is actually float scaled from int
    casacore::DataType equivalent_type(data_type);
    if ((bitpix > 0) && (bscale != 1.0) || (bzero != 0.0)) {
        equivalent_type = casacore::DataType::TpFloat;
    }
    AddDataTypeEntry(extended_info, data_type, equivalent_type);

    AddShapeEntries(extended_info, shape, spatial_axes, spectral_axis, stokes_axis, render_axes, depth_axis, axes_names);

    if (compressed_fits) {
        compressed_fits->SetShape(shape);
        compressed_fits->SetSpectralAxis(depth_axis);
        compressed_fits->SetStokesAxis(stokes_axis);
    }
}

void FileExtInfoLoader::AddDataTypeEntry(
    CARTA::FileInfoExtended& extended_info, casacore::DataType data_type, casacore::DataType equivalent_type) {
    // Report actual data type from header, and data type used by cfitsio for FITS rescaled data
    std::stringstream ss;
    ss << data_type;
    if (equivalent_type > data_type) {
        ss << " (rescaled to " << equivalent_type << ")";
    }
    auto entry = extended_info.add_computed_entries();
    entry->set_name("Data type");
    entry->set_value(ss.str());
    entry->set_entry_type(CARTA::EntryType::STRING);
}

void FileExtInfoLoader::AddShapeEntries(CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape,
    const std::vector<int>& spatial_axes, int spectral_axis, int stokes_axis, const std::vector<int>& render_axes, int depth_axis,
    casacore::Vector<casacore::String>& axes_names) {
    // Set fields/header entries for shape: dimensions, width, height, depth, stokes
    int num_dims(shape.size());
    int width(shape(render_axes[0]));
    int height(shape(render_axes[1]));
    int depth(depth_axis >= 0 ? shape(depth_axis) : 1);
    int channels(spectral_axis >= 0 ? shape(spectral_axis) : 1);
    int stokes(stokes_axis >= 0 ? shape(stokes_axis) : 1);

    extended_info.set_dimensions(num_dims);
    extended_info.set_width(width);
    extended_info.set_height(height);
    extended_info.set_depth(depth);
    extended_info.set_stokes(stokes);

    auto* axes_numbers_info = extended_info.mutable_axes_numbers();
    // Change to 1-based axis indices
    axes_numbers_info->set_spatial_x(spatial_axes[0] + 1);
    axes_numbers_info->set_spatial_y(spatial_axes[1] + 1);
    axes_numbers_info->set_spectral(spectral_axis + 1);
    axes_numbers_info->set_stokes(stokes_axis + 1);
    axes_numbers_info->set_depth(depth_axis + 1);

    if (axes_names.empty()) {
        // Set axis names with respect to axis numbers 1~4
        axes_names.assign(casacore::Vector<casacore::String>(4, "NA"));
        for (int i = 0; i < extended_info.header_entries_size(); ++i) {
            auto header_entry = extended_info.header_entries(i);
            auto entry_name = header_entry.name();
            if (entry_name.find("CTYPE") == 0) {
                auto entry_value = header_entry.value();
                if (!entry_value.empty()) {
                    entry_value = entry_value.substr(0, entry_value.find("-", 0));
                    if (entry_name.back() == '1') {
                        axes_names[0] = entry_value;
                    } else if (entry_name.back() == '2') {
                        axes_names[1] = entry_value;
                    } else if (entry_name.back() == '3') {
                        axes_names[2] = entry_value;
                    } else if (entry_name.back() == '4') {
                        axes_names[3] = entry_value;
                    }
                }
            }
        }
    }

    // In case if the stokes axis name is not available from the header info
    if (stokes_axis > -1 && axes_names[stokes_axis] == "NA") {
        axes_names[stokes_axis] = "STOKES";
    }

    // shape computed_entry
    std::string shape_string;
    switch (num_dims) {
        case 2:
            shape_string = fmt::format("[{}, {}] ({}, {})", shape(0), shape(1), axes_names[0], axes_names[1]);
            break;
        case 3:
            shape_string =
                fmt::format("[{}, {}, {}] ({}, {}, {})", shape(0), shape(1), shape(2), axes_names[0], axes_names[1], axes_names[2]);
            break;
        case 4:
            shape_string = fmt::format("[{}, {}, {}, {}] ({}, {}, {}, {})", shape(0), shape(1), shape(2), shape(3), axes_names[0],
                axes_names[1], axes_names[2], axes_names[3]);
            break;
    }
    auto shape_entry = extended_info.add_computed_entries();
    shape_entry->set_name("Shape");
    shape_entry->set_value(shape_string);
    shape_entry->set_entry_type(CARTA::EntryType::STRING);

    if (spectral_axis >= 0) {
        // header entry for number of channels
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(std::to_string(channels));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(channels);
    }
    if (stokes_axis >= 0) {
        // header entry for number of stokes
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of polarizations");
        entry->set_value(std::to_string(stokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(stokes);
    }
}

void FileExtInfoLoader::AddComputedEntries(CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image,
    const std::vector<int>& display_axes, int spectral_axis, int stokes_axis, bool use_image_for_entries) {
    // Add computed entries to extended file info
    if (use_image_for_entries) {
        // Use image coordinate system
        int display_axis0(display_axes[0]), display_axis1(display_axes[1]);

        // add computed_entries to extended info (ensures the proper order in file browser)
        casacore::CoordinateSystem coord_system(image->coordinates());
        casacore::Vector<casacore::String> axis_names = coord_system.worldAxisNames();
        casacore::Vector<casacore::String> axis_units = coord_system.worldAxisUnits();
        casacore::Vector<casacore::Double> reference_pixels = coord_system.referencePixel();
        casacore::Vector<casacore::Double> reference_values = coord_system.referenceValue();
        casacore::Vector<casacore::Double> increment = coord_system.increment();

        if (!axis_names.empty()) {
            std::string coord_type = fmt::format("{}, {}", axis_names(display_axis0), axis_names(display_axis1));
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Coordinate type");
            entry->set_value(coord_type);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (coord_system.hasDirectionCoordinate()) {
            casacore::DirectionCoordinate dir_coord(coord_system.directionCoordinate());
            std::string projection(dir_coord.projection().name());
            if ((projection == "SIN") && (dir_coord.isNCP())) {
                projection = "SIN / NCP";
            }
            if (!projection.empty()) {
                auto entry = extended_info.add_computed_entries();
                entry->set_name("Projection");
                entry->set_value(projection);
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        if (!reference_pixels.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference pixels");
            std::string ref_pix = fmt::format("[{}, {}]", reference_pixels(display_axis0) + 1.0, reference_pixels(display_axis1) + 1.0);
            entry->set_value(ref_pix);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (!reference_values.empty() && !axis_units.empty() && !axis_names.empty()) {
            // Computed entries for reference coordinates
            casacore::Quantity coord0(reference_values(display_axis0), axis_units(display_axis0));
            casacore::Quantity coord1(reference_values(display_axis1), axis_units(display_axis1));

            // Add direction coord(s) converted to angle string (RA/Dec or Lat/Long)
            // Returns Quantity string if not angle type
            std::string coord1angle =
                MakeAngleString(axis_names(display_axis0), reference_values(display_axis0), axis_units(display_axis0));
            std::string coord2angle =
                MakeAngleString(axis_names(display_axis1), reference_values(display_axis1), axis_units(display_axis1));
            std::string formatted_coords = fmt::format("[{}, {}]", coord1angle, coord2angle);
            // Add reference coords (angle format if possible)
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference coords");
            entry->set_value(formatted_coords);
            entry->set_entry_type(CARTA::EntryType::STRING);

            bool is_coord0_dir(coord0.isConform("deg")), is_coord1_dir(coord1.isConform("deg"));
            if (is_coord0_dir || is_coord1_dir) {
                // Reference coord(s) converted to deg
                std::string ref_coords_deg = fmt::format("[{}, {}]", ConvertCoordsToDeg(axis_names(display_axis0), coord0),
                    ConvertCoordsToDeg(axis_names(display_axis1), coord1));
                // Add ref coords in deg
                entry = extended_info.add_computed_entries();
                entry->set_name("Image ref coords (deg)");
                entry->set_value(ref_coords_deg);
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        if (!increment.empty() && !axis_units.empty()) {
            casacore::Quantity inc0(increment(display_axis0), axis_units(display_axis0));
            casacore::Quantity inc1(increment(display_axis1), axis_units(display_axis1));
            std::string pixel_inc = ConvertIncrementToArcsec(inc0, inc1);
            // Add increment entry
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Pixel increment");
            entry->set_value(pixel_inc);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        casacore::String brightness_unit(image->units().getName());
        if (!brightness_unit.empty()) {
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Pixel unit");
            entry->set_value(brightness_unit);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if (coord_system.hasDirectionCoordinate()) {
            // add RADESYS
            casacore::String direction_frame = casacore::MDirection::showType(coord_system.directionCoordinate().directionType());
            casacore::String radesys;
            if (direction_frame.contains("J2000")) {
                radesys = "FK5";
            } else if (direction_frame.contains("B1950")) {
                radesys = "FK4";
            }

            if (!radesys.empty() && (radesys != "ICRS")) {
                direction_frame = radesys + ", " + direction_frame;
            }

            auto entry = extended_info.add_computed_entries();
            entry->set_name("Celestial frame");
            entry->set_value(direction_frame);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }
        if (coord_system.hasSpectralAxis()) {
            casacore::String spectral_frame = casacore::MFrequency::showType(coord_system.spectralCoordinate().frequencySystem(true));
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Spectral frame");
            entry->set_value(spectral_frame);
            entry->set_entry_type(CARTA::EntryType::STRING);
            casacore::String vel_doppler = casacore::MDoppler::showType(coord_system.spectralCoordinate().velocityDoppler());
            entry = extended_info.add_computed_entries();
            entry->set_name("Velocity definition");
            entry->set_value(vel_doppler);
            entry->set_entry_type(CARTA::EntryType::STRING);
        }
    } else {
        // Use header_entries in extended info
        AddComputedEntriesFromHeaders(extended_info, display_axes, spectral_axis, stokes_axis);
    }

    casacore::ImageInfo image_info = image->imageInfo();
    if (image_info.hasBeam()) {
        const casacore::ImageBeamSet beam_set = image_info.getBeamSet();
        AddBeamEntry(extended_info, beam_set);
    }

    AddCoordRanges(extended_info, image->coordinates(), image->shape());
}

void FileExtInfoLoader::AddComputedEntriesFromHeaders(CARTA::FileInfoExtended& extended_info, const std::vector<int>& display_axes,
    int spectral_axis, int stokes_axis, CompressedFits* compressed_fits) {
    // Convert display axis1 and axis2 header_entries into computed_entries;
    // For images with missing headers or headers which casacore/wcslib cannot process.
    // Axes are 1-based for header names (ctype, cunit, etc.), 0-based for display axes
    casacore::String disp1_suffix(std::to_string(display_axes[0] + 1));
    casacore::String disp2_suffix(std::to_string(display_axes[1] + 1));
    casacore::String spectral_suffix(std::to_string(spectral_axis + 1));
    casacore::String stokes_suffix(std::to_string(stokes_axis + 1));

    double min_double(std::numeric_limits<double>::min());
    int min_int(std::numeric_limits<int>::min());

    casacore::String disp1_ctype, disp2_ctype, spectral_ctype;
    casacore::String disp1_cunit("deg"), disp2_cunit("deg"), spectral_cunit("Hz");
    casacore::String frame, radesys, specsys, bunit;
    double disp1_crval(min_double), disp1_crpix(min_double), disp1_cdelt(min_double);
    double disp2_crval(min_double), disp2_crpix(min_double), disp2_cdelt(min_double);
    double spectral_crval(min_double), spectral_crpix(min_double), spectral_cdelt(min_double);
    double stokes_crval(min_double), stokes_crpix(min_double), stokes_cdelt(min_double);
    double rest_freq(0);
    int velref(min_int);

    // Whether to add computed entries for these items
    bool found_frame(false), found_radesys(false), found_specsys(false), found_velref(false);
    bool calc_spectral_range(false), calc_stokes_range(false);

    for (int i = 0; i < extended_info.header_entries_size(); ++i) {
        auto entry = extended_info.header_entries(i);
        auto entry_name = entry.name();

        // coordinate types
        if (entry_name.find("CTYPE") == 0) {
            if (entry_name.find("CTYPE" + disp1_suffix) == 0) {
                disp1_ctype = entry.value();
                if (disp1_ctype.contains("/")) {
                    disp1_ctype = disp1_ctype.before("/");
                }
                disp1_ctype.trim();
            } else if (entry_name.find("CTYPE" + disp2_suffix) == 0) {
                disp2_ctype = entry.value();
                if (disp2_ctype.contains("/")) {
                    disp2_ctype = disp2_ctype.before("/");
                }
                disp2_ctype.trim();
            } else if (entry_name.find("CTYPE" + spectral_suffix) == 0) {
                spectral_ctype = entry.value();
                calc_spectral_range = true; // have spectral axis
            } else if (entry_name.find("CTYPE" + stokes_suffix) == 0) {
                calc_stokes_range = true; // have stokes axis
            }
        } else if (entry_name.find("CRPIX") == 0) {
            // reference pixels
            if (entry_name.find("CRPIX" + disp1_suffix) == 0) {
                disp1_crpix = entry.numeric_value();
            } else if (entry_name.find("CRPIX" + disp2_suffix) == 0) {
                disp2_crpix = entry.numeric_value();
            } else if (entry_name.find("CRPIX" + spectral_suffix) == 0) {
                spectral_crpix = entry.numeric_value();
            } else if (entry_name.find("CRPIX" + stokes_suffix) == 0) {
                stokes_crpix = entry.numeric_value();
            }
        } else if (entry_name.find("CRVAL") == 0) {
            // reference values
            if (entry_name.find("CRVAL" + disp1_suffix) == 0) {
                disp1_crval = entry.numeric_value();
            } else if (entry_name.find("CRVAL" + disp2_suffix) == 0) {
                disp2_crval = entry.numeric_value();
            } else if (entry_name.find("CRVAL" + spectral_suffix) == 0) {
                spectral_crval = entry.numeric_value();
            } else if (entry_name.find("CRVAL" + stokes_suffix) == 0) {
                stokes_crval = entry.numeric_value();
            }
        } else if (entry_name.find("CDELT") == 0) {
            // pixel increment
            if (entry_name.find("CDELT" + disp1_suffix) == 0) {
                disp1_cdelt = entry.numeric_value();
            } else if (entry_name.find("CDELT" + disp2_suffix) == 0) {
                disp2_cdelt = entry.numeric_value();
            } else if (entry_name.find("CDELT" + spectral_suffix) == 0) {
                spectral_cdelt = entry.numeric_value();
            } else if (entry_name.find("CDELT" + stokes_suffix) == 0) {
                stokes_cdelt = entry.numeric_value();
            }
        } else if (entry_name.find("CUNIT") == 0) {
            // coordinate units
            if (entry_name.find("CUNIT" + disp1_suffix) == 0) {
                disp1_cunit = entry.value();
                if (disp1_cunit.contains("/")) {
                    disp1_cunit = disp1_ctype.before("/");
                }
                NormalizeHeaderUnit(disp1_cunit);
            } else if (entry_name.find("CUNIT" + disp2_suffix) == 0) {
                disp2_cunit = entry.value();
                if (disp2_cunit.contains("/")) {
                    disp2_cunit = disp2_cunit.before("/");
                }
                NormalizeHeaderUnit(disp2_cunit);
            } else if (entry_name.find("CUNIT" + spectral_suffix) == 0) {
                spectral_cunit = entry.value();
                NormalizeHeaderUnit(spectral_cunit);
            }
        } else if (!found_frame &&
                   ((entry_name.find("EQUINOX") == 0) || (entry_name.find("TELEQUIN") == 0) || (entry_name.find("EPOCH") == 0))) {
            // Celestial frame
            frame = entry.value();
            double frame_numval = entry.numeric_value();
            found_frame = true;

            if (frame.contains("2000") || (frame_numval == 2000.0)) {
                frame = "J2000";
            } else if (frame.contains("1950") || (frame_numval == 1950.0)) {
                frame = "B1950";
            }
        } else if (entry_name.find("RADESYS") == 0) {
            radesys = entry.value();
            found_radesys = true;
        } else if (entry_name.find("SPECSYS") == 0) {
            specsys = entry.value();
            found_specsys = true;
        } else if (entry_name.find("VELREF") == 0) {
            velref = entry.numeric_value();
            found_velref = true;
        } else if (entry_name.find("BUNIT") == 0) {
            bunit = entry.value();
            NormalizeHeaderUnit(bunit);
        } else if ((entry_name.find("RESTFREQ") == 0) || (entry_name.find("RESTFRQ") == 0)) {
            rest_freq = entry.numeric_value();
        }
    }

    // Set computed entries
    std::string projection;
    if (!disp1_ctype.empty() && !disp2_ctype.empty()) {
        GetCoordNames(disp1_ctype, disp2_ctype, radesys, projection);
        found_radesys = !radesys.empty();

        std::string coord_type = fmt::format("{}, {}", disp1_ctype, disp2_ctype);
        auto comp_entry = extended_info.add_computed_entries();
        comp_entry->set_name("Coordinate type");
        comp_entry->set_value(coord_type);
        comp_entry->set_entry_type(CARTA::EntryType::STRING);
        if (!projection.empty()) {
            auto comp_entry = extended_info.add_computed_entries();
            comp_entry->set_name("Projection");
            comp_entry->set_value(projection);
            comp_entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (disp1_crpix != min_double && disp2_crpix != min_double) {
        std::string ref_pix = fmt::format("[{}, {}]", disp1_crpix, disp2_crpix);
        auto comp_entry = extended_info.add_computed_entries();
        comp_entry->set_name("Image reference pixels");
        comp_entry->set_value(ref_pix);
        comp_entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (disp1_crval != min_double && disp2_crval != min_double) {
        std::string coord1 = MakeAngleString(disp1_ctype, disp1_crval, disp1_cunit);
        std::string coord2 = MakeAngleString(disp2_ctype, disp2_crval, disp2_cunit);
        std::string ref_coords = fmt::format("[{}, {}]", coord1, coord2);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Image reference coords");
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);

        // reference coordinates in deg if can convert
        casacore::Quantity q1(disp1_crval, disp1_cunit);
        casacore::Quantity q2(disp2_crval, disp2_cunit);
        if (q1.isConform("deg") || q2.isConform("deg")) {
            // Reference coord(s) converted to deg
            std::string ref_coords_deg = fmt::format("[{}, {}]", ConvertCoordsToDeg(disp1_ctype, q1), ConvertCoordsToDeg(disp2_ctype, q2));
            auto comp_entry = extended_info.add_computed_entries();
            comp_entry->set_name("Image ref coords (deg)");
            comp_entry->set_value(ref_coords_deg);
            comp_entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (disp1_cdelt != min_double && disp2_cdelt != min_double) {
        casacore::Quantity inc1(disp1_cdelt, disp1_cunit);
        casacore::Quantity inc2(disp2_cdelt, disp2_cunit);
        std::string pixel_inc = ConvertIncrementToArcsec(inc1, inc2);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Pixel increment");
        entry->set_value(pixel_inc);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!bunit.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Pixel unit");
        entry->set_value(bunit);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (found_radesys || found_frame) {
        std::string direction_frame;
        if (found_frame) {
            if (frame == "J2000") {
                direction_frame = "FK5, J2000";
            } else if (frame == "B1950") {
                direction_frame = "FK4, B1950";
            } else if (frame == "ICRS") {
                direction_frame = "ICRS, J2000";
            } else {
                direction_frame = frame;
            }
        } else if (found_radesys) {
            direction_frame = radesys;
        } else {
            direction_frame = fmt::format("{}, {}", radesys, frame);
        }
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(direction_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!found_specsys) {
        // Set if appended to CTYPE3 rather than in SPECSYS
        SplitCtypeDescriptor(spectral_ctype, specsys);
    }

    if (!specsys.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(specsys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (found_velref) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Velocity definition");
        // VELREF : 1 LSR, 2 HEL, 3 OBS, +256 Radio
        if (velref < 4) {
            entry->set_value("OPTICAL");
        } else if (velref > 256) {
            entry->set_value("RADIO");
        } else {
            entry->set_value("UNKNOWN");
        }
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (compressed_fits) {
        casacore::CoordinateSystem coordsys;
        auto shape = compressed_fits->GetShape();

        // Make a direction coordinate and add it to the coordinate system
        casacore::MDirection::Types frame_type;
        if (casacore::MDirection::getType(frame_type, frame) || casacore::MDirection::getType(frame_type, radesys)) {
            auto xform = compressed_fits->GetTransformMatrix();
            auto proj_type = casacore::Projection::type(projection);
            auto to_rad = casacore::C::pi / 180.0;
            casacore::DirectionCoordinate dir_coord(frame_type, proj_type, disp1_crval * to_rad, disp2_crval * to_rad, disp1_cdelt * to_rad,
                disp2_cdelt * to_rad, xform, disp1_crpix - 1, disp2_crpix - 1);
            casacore::Vector<casacore::String> units(2);
            units = "rad";
            dir_coord.setWorldAxisUnits(units);
            coordsys.addCoordinate(dir_coord);
        }

        // Make a spectral coordinate (if any) and add it to the coordinate system
        if (calc_spectral_range) {
            casacore::MFrequency::Types frequency_type;
            if (casacore::MFrequency::getType(frequency_type, specsys)) {
                // Convert velocities to frequencies
                casacore::SpectralCoordinate spectral_coordinate;
                double fits_offset(1.0); // FITS is one-based

                if (spectral_ctype == "WAV" || spectral_ctype == "AWAV" || spectral_ctype == "VELO") {
                    // Create spectral coordinate with values
                    size_t num_chan = shape(spectral_axis);
                    casacore::Vector<casacore::Double> values(num_chan);
                    for (size_t i = 0; i < num_chan; ++i) {
                        values(i) = spectral_crval + (spectral_cdelt * (double(i) + fits_offset - spectral_crpix));
                    }

                    if (spectral_ctype == "VELO") { // values are velocities
                        spectral_coordinate =
                            casacore::SpectralCoordinate(frequency_type, casacore::MDoppler::RADIO, values, spectral_cunit, rest_freq);
                    } else { // values are wavelengths
                        spectral_coordinate = casacore::SpectralCoordinate(frequency_type, values, spectral_cunit, rest_freq);
                    }
                } else {
                    // Create spectral coordinate with center frequency values
                    if (spectral_ctype == "VRAD") { // For radio velocity (Hz)
                        spectral_crval = rest_freq * (fits_offset - spectral_crval / casacore::C::c);
                        spectral_cdelt = -rest_freq * (spectral_cdelt / casacore::C::c);
                    } else if (spectral_ctype == "VOPT") { // For optical velocity (Hz)
                        spectral_crval = rest_freq / (fits_offset + spectral_crval / casacore::C::c);
                        spectral_cdelt = rest_freq / (fits_offset + spectral_cdelt / casacore::C::c);
                    }
                    spectral_coordinate =
                        casacore::SpectralCoordinate(frequency_type, spectral_crval, spectral_cdelt, spectral_crpix, rest_freq);
                }
                coordsys.addCoordinate(spectral_coordinate);
            }
        }

        // Make a stokes coordinate (if any) and add it to the coordinate system
        if (calc_stokes_range) {
            if (stokes_crpix != 1) { // Get the stokes type for the first pixel
                stokes_crval -= stokes_cdelt * (stokes_crpix - 1);
            }

            int stokes_size = shape[stokes_axis];
            casacore::Vector<casacore::Int> stokes_types(stokes_size);
            for (int i = 0; i < stokes_size; ++i) {
                stokes_types[i] = stokes_crval + stokes_cdelt * i;
            }
            casacore::StokesCoordinate stokes_coord(stokes_types);
            coordsys.addCoordinate(stokes_coord);
        }

        AddCoordRanges(extended_info, coordsys, shape);
    }
}

void FileExtInfoLoader::AddBeamEntry(CARTA::FileInfoExtended& extended_info, const casacore::ImageBeamSet& beam_set) {
    // Add restoring/median beam to computed entries.
    casacore::GaussianBeam gaussian_beam;
    std::string entry_name;
    if (beam_set.hasSingleBeam()) {
        gaussian_beam = beam_set.getBeam();
        entry_name = "Restoring beam";
    } else if (beam_set.hasMultiBeam()) {
        gaussian_beam = beam_set.getMedianAreaBeam();
        entry_name = "Median area beam";
    }

    if (!gaussian_beam.isNull()) {
        std::string beam_info = fmt::format("{:g}\" X {:g}\", {:g} deg", gaussian_beam.getMajor("arcsec"), gaussian_beam.getMinor("arcsec"),
            gaussian_beam.getPA(casacore::Unit("deg")));

        auto entry = extended_info.add_computed_entries();
        entry->set_name(entry_name);
        entry->set_entry_type(CARTA::EntryType::STRING);
        entry->set_value(beam_info);
    }
}

void FileExtInfoLoader::NormalizeHeaderUnit(casacore::String& unit) {
    // Check that string is in unit map with various cases
    if (casacore::UnitVal::check(unit)) {
        return;
    }
    casacore::String converted_unit(unit);
    converted_unit.capitalize();
    if (casacore::UnitVal::check(converted_unit)) {
        unit = converted_unit;
        return;
    }
    converted_unit.downcase();
    converted_unit = (converted_unit.startsWith("deg") ? "deg" : converted_unit); // shorten "degrees"
    if (casacore::UnitVal::check(converted_unit)) {
        unit = converted_unit;
        return;
    }
    // keep unit
}

std::string FileExtInfoLoader::MakeAngleString(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    if (unit.empty()) {
        return fmt::format("{:.6g}", val);
    }

    casacore::MVAngle::formatTypes format;
    if (type == "Right Ascension") {
        format = casacore::MVAngle::TIME;
    } else if ((type == "Declination") || (type.find("Longitude") != std::string::npos) || (type.find("Latitude") != std::string::npos)) {
        format = casacore::MVAngle::ANGLE;
    } else {
        return fmt::format("{:.6g} {}", val, unit);
    }

    casacore::Quantity quant1(val, unit);
    if (!quant1.isConform("deg")) {
        // Cannot convert to MVAngle format
        return fmt::format("{:.6g} {}", val, unit);
    }

    if (type.find("Longitude") != std::string::npos && quant1.get("deg").getValue() < 0) {
        casacore::Quantity pi2(360, "deg");
        quant1 += pi2;
    }
    casacore::MVAngle mva(quant1);
    return mva.string(format, 10);
}

std::string FileExtInfoLoader::ConvertCoordsToDeg(const std::string& type, const casacore::Quantity& coord) {
    // If possible, convert quantities to degrees. Return formatted string
    casacore::Quantity coord_deg(coord), pi2(360, "deg");
    if (coord.isConform("deg")) {
        coord_deg = (type == "Longitude" && coord.get("deg").getValue() < 0) ? (coord + pi2).get("deg") : coord.get("deg");
    }
    return fmt::format("{}", coord_deg);
}

std::string FileExtInfoLoader::ConvertIncrementToArcsec(const casacore::Quantity& inc0, const casacore::Quantity& inc1) {
    // Convert to arcsec, use unit symbol "
    casacore::String unit0(inc0.getUnit()), unit1(inc1.getUnit());
    casacore::Quantity inc0_arcsec(inc0), inc1_arcsec(inc1);

    if (inc0.isConform("arcsec")) {
        inc0_arcsec = inc0.get("arcsec");
        unit0 = "\"";
    } else {
        unit0 = " " + unit0;
    }

    if (inc1.isConform("arcsec")) {
        inc1_arcsec = inc1.get("arcsec");
        unit1 = "\"";
    } else {
        unit1 = " " + unit1;
    }

    std::string inc_format = "{:.6g}" + unit0 + ", {:.6g}" + unit1;
    return fmt::format(inc_format, inc0_arcsec.getValue(), inc1_arcsec.getValue());
}

void FileExtInfoLoader::GetCoordNames(std::string& ctype1, std::string& ctype2, std::string& radesys, std::string& projection) {
    // split ctype1 and ctype2 into type and projection
    std::unordered_map<std::string, std::string> names = {
        {"RA", "Right Ascension"}, {"DEC", "Declination"}, {"GLON", "Longitude"}, {"GLAT", "Latitude"}};

    // ctype1 to coordinate name and projection
    SplitCtypeDescriptor(ctype1, projection);
    if (radesys.empty() && (ctype1 == "GLON")) {
        radesys = "GALACTIC";
    }
    if (names.find(ctype1) != names.end()) {
        ctype1 = names[ctype1];
    }

    // ctype2 to coordinate name
    std::string projection2;
    SplitCtypeDescriptor(ctype2, projection2);
    if (names.find(ctype2) != names.end()) {
        ctype2 = names[ctype2];
    }
}

void FileExtInfoLoader::SplitCtypeDescriptor(std::string& ctype, std::string& descriptor) {
    // Splits CTYPE string on one or more '-', returns coord name and descriptor
    auto delim_pos = ctype.find_first_of("-");
    if (delim_pos != std::string::npos) {
        // Get descriptor then remove it from ctype
        descriptor = ctype.substr(delim_pos, std::string::npos);
        ctype = ctype.substr(0, delim_pos);
        auto desc_start = descriptor.find_first_not_of('-');
        descriptor = descriptor.substr(desc_start, std::string::npos);
    }
}

void FileExtInfoLoader::AddCoordRanges(
    CARTA::FileInfoExtended& extended_info, const casacore::CoordinateSystem& coord_system, const casacore::IPosition& image_shape) {
    if (image_shape.empty()) {
        return;
    }

    if (coord_system.hasDirectionCoordinate()) {
        auto direction_coord = coord_system.directionCoordinate();
        if (direction_coord.referenceValue().size() == 2) {
            casacore::Vector<int> spatial_axes = coord_system.directionAxesNumbers();
            casacore::Vector<casacore::String> axis_names = coord_system.worldAxisNames();
            casacore::Vector<double> pixels(2, 0);
            casacore::Vector<double> world(2, 0);
            casacore::String units;
            double x_min(std::numeric_limits<double>::max()), x_max(std::numeric_limits<double>::lowest());
            double y_min(std::numeric_limits<double>::max()), y_max(std::numeric_limits<double>::lowest());

            auto get_xy_minmax = [&](double pixel_x, double pixel_y) {
                pixels[0] = pixel_x;
                pixels[1] = pixel_y;
                direction_coord.toWorld(world, pixels);

                if (world[0] < x_min) {
                    x_min = world[0];
                }
                if (world[0] > x_max) {
                    x_max = world[0];
                }
                if (world[1] < y_min) {
                    y_min = world[1];
                }
                if (world[1] > y_max) {
                    y_max = world[1];
                }
            };

            double x_max_pixel = image_shape[spatial_axes[0]] - 1;
            double y_max_pixel = image_shape[spatial_axes[1]] - 1;
            get_xy_minmax(0, 0);
            get_xy_minmax(x_max_pixel, y_max_pixel);
            get_xy_minmax(0, y_max_pixel);
            get_xy_minmax(x_max_pixel, 0);

            // Get start world coord
            std::string x_start = direction_coord.format(units, casacore::Coordinate::DEFAULT, x_min, 0, true, true);
            std::string y_start = direction_coord.format(units, casacore::Coordinate::DEFAULT, y_min, 1, true, true);

            // Get end world coord
            std::string x_end = direction_coord.format(units, casacore::Coordinate::DEFAULT, x_max, 0, true, true);
            std::string y_end = direction_coord.format(units, casacore::Coordinate::DEFAULT, y_max, 1, true, true);

            // Set x and y coordinate names
            if (spatial_axes[0] > -1 && spatial_axes[0] < axis_names.size()) {
                if (axis_names(spatial_axes[0]) == "Right Ascension") {
                    axis_names(spatial_axes[0]) = "RA";
                } else if (axis_names(spatial_axes[0]) == "Longitude") {
                    axis_names(spatial_axes[0]) = "LON";
                }
                auto* x_entry = extended_info.add_computed_entries();
                x_entry->set_name(fmt::format("{} range", axis_names(spatial_axes[0])));
                x_entry->set_value(fmt::format("[{}, {}]", x_start, x_end));
                x_entry->set_entry_type(CARTA::EntryType::STRING);
            }

            if (spatial_axes[1] > -1 && spatial_axes[1] < axis_names.size()) {
                if (axis_names(spatial_axes[1]) == "Declination") {
                    axis_names(spatial_axes[1]) = "DEC";
                } else if (axis_names(spatial_axes[1]) == "Latitude") {
                    axis_names(spatial_axes[1]) = "LAT";
                }
                auto* y_entry = extended_info.add_computed_entries();
                y_entry->set_name(fmt::format("{} range", axis_names(spatial_axes[1])));
                y_entry->set_value(fmt::format("[{}, {}]", y_start, y_end));
                y_entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }
    }

    if (coord_system.hasSpectralAxis() && image_shape[coord_system.spectralAxisNumber()] > 0) {
        auto spectral_coord = coord_system.spectralCoordinate();
        casacore::Vector<casacore::String> spectral_units = spectral_coord.worldAxisUnits();
        spectral_units(0) = (spectral_units(0) == "Hz" ? "GHz" : spectral_units(0));
        casacore::String velocity_units("km/s");

        // Convert to desired units
        spectral_coord.setWorldAxisUnits(spectral_units);
        spectral_coord.setVelocity(velocity_units);

        double start_pixel = 0;
        double end_pixel = image_shape[coord_system.spectralAxisNumber()] - 1;

        double start_frequency, end_frequency;
        double start_velocity, end_velocity;
        bool has_spectral_range(end_pixel > start_pixel);

        if (spectral_coord.toWorld(start_frequency, start_pixel)) {
            auto* frequency_entry = extended_info.add_computed_entries();
            if (has_spectral_range && spectral_coord.toWorld(end_frequency, end_pixel)) {
                casacore::Quantity end_freq_quant(end_frequency, spectral_units(0));
                frequency_entry->set_name("Frequency range");
                frequency_entry->set_value(fmt::format("[{:.4f}, {:.4f}] ({})", start_frequency, end_frequency, spectral_units(0)));
            } else {
                frequency_entry->set_name("Frequency");
                frequency_entry->set_value(fmt::format("{:.4f} ({})", start_frequency, spectral_units(0)));
            }
            frequency_entry->set_entry_type(CARTA::EntryType::STRING);
        }

        if ((spectral_coord.restFrequency() != 0.0) && spectral_coord.pixelToVelocity(start_velocity, start_pixel)) {
            auto* velocity_entry = extended_info.add_computed_entries();
            if (has_spectral_range && spectral_coord.pixelToVelocity(end_velocity, end_pixel)) {
                velocity_entry->set_name("Velocity range");
                velocity_entry->set_value(fmt::format("[{:.4f}, {:.4f}] ({})", start_velocity, end_velocity, velocity_units));
            } else { // For a single channel image
                velocity_entry->set_name("Velocity");
                velocity_entry->set_value(fmt::format("{:.4f} ({})", start_velocity, velocity_units));
            }
            velocity_entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (coord_system.hasPolarizationAxis() && image_shape[coord_system.polarizationAxisNumber()] > 0) {
        auto stokes_coord = coord_system.stokesCoordinate();
        std::string stokes;
        auto stokes_vec = stokes_coord.stokesStrings();
        for (int i = 0; i < stokes_vec.size(); ++i) {
            stokes += stokes_vec[i] + ", ";
        }
        stokes = stokes.size() > 2 ? stokes.substr(0, stokes.size() - 2) : stokes;

        auto* stokes_entry = extended_info.add_computed_entries();
        stokes_entry->set_name("Stokes coverage");
        stokes_entry->set_value(fmt::format("[{}]", stokes));
        stokes_entry->set_entry_type(CARTA::EntryType::STRING);
    }
}

casacore::Vector<casacore::String> FileExtInfoLoader::FitsHeaderStrings(casacore::String& name, unsigned int hdu) {
    // Use fitsio to get header strings from FITS image
    fitsfile* fptr;
    int status(0);
    int* hdutype(nullptr);

    // Open file and move to hdu
    fits_open_file(&fptr, name.c_str(), 0, &status);
    status = 0;
    fits_movabs_hdu(fptr, hdu + 1, hdutype, &status);

    // Get number of headers
    int nkeys(0);
    int* more_keys(nullptr);
    status = 0;
    fits_get_hdrspace(fptr, &nkeys, more_keys, &status);

    // Get headers as single string, free memory, close file
    int no_comments(0); // false, get the comments
    char* header[nkeys];
    status = 0;
    fits_hdr2str(fptr, no_comments, nullptr, 0, header, &nkeys, &status);
    std::string header_str = std::string(header[0]);

    // Free memory and close file
    status = 0;
    fits_free_memory(*header, &status);
    status = 0;
    fits_close_file(fptr, &status);

    // Divide header string into 80-char FITS cards
    casacore::Vector<casacore::String> header_strings(nkeys);
    size_t pos(0);

    for (int i = 0; i < nkeys; ++i) {
        header_strings[i] = header_str.substr(pos, 80);

        if (header_strings[i].contains("FELO-HEL")) {
            header_strings.resize();
            return header_strings;
        }

        pos += 80;
    }

    return header_strings;
}

bool FileExtInfoLoader::GetFITSHeader(std::shared_ptr<casacore::ImageInterface<float>> image, const std::string& hdu,
    casacore::ImageFITSHeaderInfo& fhi, casacore::String& error_string) {
    bool prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength;
    GetSpectralCoordPreferences(image.get(), prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength);

    casacore::String origin_string;
    bool stokes_last(false), degenerate_last(false), verbose(true), allow_append(false), history(true);
    bool prim_head(hdu == "0");
    int bit_pix(-32);
    float min_pix(1.0), max_pix(-1.0);

    bool ok = casacore::ImageFITSConverter::ImageHeaderToFITS(error_string, fhi, *(image.get()), prefer_velocity, optical_velocity, bit_pix,
        min_pix, max_pix, degenerate_last, verbose, stokes_last, prefer_wavelength, air_wavelength, prim_head, allow_append, origin_string,
        history);

    // ok can be false when unrecognized keyword or have to shorten keyword/value, but keyword list is still set.
    // Return false only if no keyword list set.
    if (!ok && fhi.kw.isempty()) {
        return false;
    }

    return true;
}
