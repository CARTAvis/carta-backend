/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileExtInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileExtInfoLoader.h"

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
#include "../ImageData/CompressedFits.h"
#include "FileList/FitsHduList.h"
#include "Logger/Logger.h"
#include "Util/Casacore.h"
#include "Util/File.h"
#include "Util/FileSystem.h"

using namespace carta;

FileExtInfoLoader::FileExtInfoLoader(carta::FileLoader* loader) : _loader(loader) {}

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
            AddInitialComputedEntries(hdu_info.first, hdu_info.second, filename, render_axes);

            // Use headers in FileInfoExtended to create computed entries
            AddComputedEntriesFromHeaders(hdu_info.second, render_axes);

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
        message = "No image hdus found.";
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
            auto image = _loader->GetImage();

            if (image) {
                // Check dimensions
                casacore::IPosition image_shape(image->shape());
                unsigned int num_dim = image_shape.size();

                if (num_dim < 2 || num_dim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return info_ok;
                }

                // Name of image class: special cases for FITS
                casacore::String image_type(image->imageType());
                bool is_casacore_fits(image_type == "FITSImage");
                bool is_carta_fits(image_type == "CartaFitsImage");
                bool is_carta_hdf5(image_type == "CartaHdf5Image");

                int bitpix(0);
                if (is_casacore_fits || is_carta_fits) {
                    bitpix = GetFitsBitpix(image.get());
                }

                // For computed entries:
                casacore::String extname, radesys;
                bool use_image_for_entries(true);

                casacore::String stokes_ctype_num; // used to set stokes values in loader

                if (is_carta_hdf5) {
                    carta::CartaHdf5Image* hdf5_image = dynamic_cast<carta::CartaHdf5Image*>(image.get());
                    casacore::Vector<casacore::String> headers = hdf5_image->FITSHeaderStrings();

                    for (auto& header : headers) {
                        // Parse header into name, value, comment (if exist)
                        casacore::String name(header), value, comment;
                        auto eq_pos = header.find('=', 0);
                        bool quoted_value(false);

                        if (eq_pos != std::string::npos) {
                            name = header.substr(0, eq_pos);
                            name.trim();
                            value = header.substr(eq_pos + 1);
                            value.trim();

                            auto end_quote = std::string::npos;
                            if (value[0] == '\'') {
                                quoted_value = true;
                                end_quote = value.find('\'', 1);
                            } else if (value[0] == '"') {
                                quoted_value = true;
                                end_quote = value.find('"', 1);
                            }

                            if (quoted_value) {
                                value = value.substr(1, end_quote - 1);

                                auto slash_pos = header.find('/', end_quote + 1);
                                if (slash_pos != std::string::npos) {
                                    comment = header.substr(slash_pos + 1);
                                    comment.trim();
                                }
                            } else {
                                auto slash_pos = header.find('/', eq_pos);
                                if (slash_pos == std::string::npos) {
                                    value = header.substr(eq_pos + 1);
                                    value.trim();
                                } else {
                                    value = header.substr(eq_pos + 1, slash_pos - (eq_pos + 1));
                                    value.trim();
                                    comment = header.substr(slash_pos + 1);
                                    comment.trim();
                                }
                            }

                            if (name == "SIMPLE") {
                                try {
                                    int numval = std::stoi(value);
                                    value = (numval ? "T" : "F");
                                } catch (std::invalid_argument& err) {
                                    // not numeric
                                }
                            }
                        }

                        if (!name.empty() && (name != "END")) {
                            if (name == "EXTNAME") {
                                extname = value;
                            } else if (name == "RADESYS") {
                                radesys = value;
                            } else if (name.startsWith("CTYPE") && ((value == "STOKES") || (value == "Stokes") || (value == "stokes"))) {
                                stokes_ctype_num = name.back();
                            }

                            auto entry = extended_info.add_header_entries();
                            entry->set_name(name);

                            if (!value.empty()) {
                                *entry->mutable_value() = value;

                                if (!quoted_value) {
                                    // try to convert value to numeric
                                    if (value.contains(".")) {
                                        try {
                                            double dvalue = std::stod(value);
                                            entry->set_numeric_value(dvalue);
                                            entry->set_entry_type(CARTA::EntryType::FLOAT);

                                            if (name == ("CRVAL" + stokes_ctype_num)) {
                                                _loader->SetFirstStokesType((int)dvalue);
                                            } else if (name == ("CDELT" + stokes_ctype_num)) {
                                                _loader->SetDeltaStokesIndex((int)dvalue);
                                            }
                                        } catch (std::invalid_argument) {
                                            // Not a number - set string value only
                                            entry->set_entry_type(CARTA::EntryType::STRING);
                                        } catch (std::out_of_range) {
                                            try {
                                                char* endptr(nullptr);
                                                long double ldvalue = std::strtold(value.c_str(), &endptr);
                                                entry->set_numeric_value(ldvalue);
                                                entry->set_entry_type(CARTA::EntryType::FLOAT);
                                            } catch (std::out_of_range) {
                                                entry->set_entry_type(CARTA::EntryType::STRING);
                                            }
                                        }
                                    } else {
                                        try {
                                            // int numeric value
                                            int ivalue = std::stoi(value);
                                            entry->set_numeric_value(ivalue);
                                            entry->set_entry_type(CARTA::EntryType::INT);
                                        } catch (std::invalid_argument) {
                                            // Not a number - set string value only
                                            entry->set_entry_type(CARTA::EntryType::STRING);
                                        } catch (std::out_of_range) {
                                            try {
                                                // long numeric value
                                                long lvalue = std::stol(value);
                                                entry->set_numeric_value(lvalue);
                                                entry->set_entry_type(CARTA::EntryType::INT);
                                            } catch (std::out_of_range) {
                                                entry->set_entry_type(CARTA::EntryType::STRING);
                                            }
                                        }
                                    }
                                }
                            }

                            if (!comment.empty()) {
                                entry->set_comment(comment);
                            }
                        }
                    }
                    use_image_for_entries = false;
                } else {
                    // Add FitsKeywordList to ImageFITSHeaderInfo
                    casacore::ImageFITSHeaderInfo fhi;

                    if (is_casacore_fits) {
                        // Get original headers
                        casacore::String filename(image->name());
                        casacore::FitsInput fits_input(filename.c_str(), casacore::FITS::Disk);

                        if (fits_input.err()) {
                            message = "Error opening FITS file.";
                            return false;
                        }

                        unsigned int hdu_num(FileInfo::GetFitsHdu(hdu));

                        for (unsigned int ihdu = 0; ihdu < hdu_num; ++ihdu) {
                            fits_input.skip_hdu();
                            if (fits_input.err()) {
                                message = "Error advancing to requested hdu.";
                                return false;
                            }
                        }

                        casacore::FitsKeywordList kwlist;
                        if (GetFitsKwList(fits_input, hdu_num, kwlist)) {
                            fhi.kw = kwlist;
                            use_image_for_entries = false;
                        }
                    } else {
                        bool prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength;
                        GetSpectralCoordPreferences(image.get(), prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength);

                        // Get image headers in FITS format
                        casacore::String error_string, origin_string;
                        bool stokes_last(false), degenerate_last(false), verbose(false), allow_append(false), history(false);
                        bool prim_head(hdu == "0");
                        int bit_pix(-32);
                        float min_pix(1.0), max_pix(-1.0);

                        if (!casacore::ImageFITSConverter::ImageHeaderToFITS(error_string, fhi, *(image.get()), prefer_velocity,
                                optical_velocity, bit_pix, min_pix, max_pix, degenerate_last, verbose, stokes_last, prefer_wavelength,
                                air_wavelength, prim_head, allow_append, origin_string, history)) {
                            message = error_string;
                            return info_ok;
                        }
                    }

                    FitsHeaderInfoToHeaderEntries(fhi, use_image_for_entries, bitpix, hdu, extended_info);
                }

                int spectral_axis, depth_axis, stokes_axis;
                if (_loader->FindCoordinateAxes(image_shape, spectral_axis, depth_axis, stokes_axis, message)) {
                    // Computed entries for rendered image axes (not always 0 and 1)
                    std::vector<int> render_axes = _loader->GetRenderAxes();

                    // Describe rendered axes
                    AddShapeEntries(extended_info, image_shape, spectral_axis, depth_axis, stokes_axis, render_axes);
                    AddComputedEntries(extended_info, image.get(), render_axes, radesys, use_image_for_entries);

                    info_ok = true;
                }
            } else { // image failed
                message = "Image could not be opened.";
            }

            _loader->CloseImageIfUpdated();
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
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

void FileExtInfoLoader::FitsHeaderInfoToHeaderEntries(casacore::ImageFITSHeaderInfo& fhi, bool using_image_header, int bitpix,
    const std::string& hdu, CARTA::FileInfoExtended& extended_info) {
    // Fill FileInfoExtended header_entries from ImageFITSHeaderInfo and begin computed_entries.
    // Modifies FileInfoExtended and returns RADESYS value for computed_entries.

    // Axis or coord number to append to name
    int naxis(0), ntype(1), nval(1), ndelt(1), npix(1);

    casacore::String stokes_axis_num; // stokes values for loader

    std::string extname; // for initial computed_entries

    // Create FileInfoExtended header_entries for each FitsKeyword
    fhi.kw.first(); // go to first card
    casacore::FitsKeyword* fkw = fhi.kw.next();

    while (fkw) {
        casacore::String name(fkw->name());
        casacore::String comment(fkw->comm());

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
        } else if (name == "H5SCHEMA") { // was shortened to FITS length 8
            name = "SCHEMA_VERSION";
        } else if (name == "H5CNVRTR") { // was shortened to FITS length 8
            name = "HDF5_CONVERTER";
        } else if (name == "H5CONVSN") { // was shortened to FITS length 8
            name = "HDF5_CONVERTER_VERSION";
        } else if (name == "H5DATE") { // was shortened to FITS length 8
            name = "HDF5_DATE";
        }

        // Fill the first stokes type and the delta value for the stokes index. Set the first stokes type index as 0
        if (!stokes_axis_num.empty()) {
            if (name == ("CRVAL" + stokes_axis_num)) {
                _loader->SetFirstStokesType((int)fkw->asDouble());
            } else if (name == ("CDELT" + stokes_axis_num)) {
                _loader->SetDeltaStokesIndex((int)fkw->asDouble());
            }
        }

        if (name != "END") {
            switch (fkw->type()) {
                case casacore::FITS::LOGICAL: {
                    bool value(fkw->asBool());
                    std::string bool_string(value ? "T" : "F");

                    auto header_entry = extended_info.add_header_entries();
                    header_entry->set_name(name);
                    *header_entry->mutable_value() = bool_string;
                    header_entry->set_entry_type(CARTA::EntryType::INT);
                    header_entry->set_numeric_value(value);
                    header_entry->set_comment(comment);
                    break;
                }
                case casacore::FITS::LONG: {
                    int value(fkw->asInt());

                    if ((name.find("BITPIX") != std::string::npos) && (bitpix != 0)) {
                        // Use internal datatype for bitpix value (since always -32 in header conversion)
                        value = bitpix;
                        comment.clear();
                    }

                    std::string string_value = fmt::format("{:d}", value);

                    auto header_entry = extended_info.add_header_entries();
                    header_entry->set_name(name);
                    *header_entry->mutable_value() = string_value;
                    header_entry->set_entry_type(CARTA::EntryType::INT);
                    header_entry->set_numeric_value(value);
                    header_entry->set_comment(comment);
                    break;
                }
                case casacore::FITS::BYTE:
                case casacore::FITS::SHORT:
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

                    auto header_entry = extended_info.add_header_entries();
                    header_entry->set_name(name);
                    *header_entry->mutable_value() = string_value;
                    header_entry->set_entry_type(CARTA::EntryType::FLOAT);
                    header_entry->set_numeric_value(value);
                    header_entry->set_comment(comment);
                    break;
                }
                case casacore::FITS::STRING:
                case casacore::FITS::FSTRING: {
                    // Do not include ORIGIN (casacore) or DATE (current) added by ImageHeaderToFITS
                    if (!using_image_header || (using_image_header && ((name != "DATE") && (name != "ORIGIN")))) {
                        casacore::String header_string = fkw->asString();
                        header_string.trim();

                        // save for computed_entries
                        if (name == "EXTNAME") {
                            extname = header_string;
                        }

                        if (name.contains("CTYPE")) {
                            if (header_string.contains("FREQ")) {
                                // Fix header with "FREQUENCY"
                                header_string = "FREQ";
                            } else if (header_string.startsWith("STOKES")) {
                                // Found CTYPEX = STOKES; set the stokes axis number X
                                stokes_axis_num = name.back();
                            }
                        }

                        auto header_entry = extended_info.add_header_entries();
                        header_entry->set_name(name);
                        *header_entry->mutable_value() = header_string;
                        header_entry->set_entry_type(CARTA::EntryType::STRING);
                        header_entry->set_comment(comment);
                    }
                    break;
                }
                case casacore::FITS::BIT:
                case casacore::FITS::CHAR:
                case casacore::FITS::COMPLEX:
                case casacore::FITS::ICOMPLEX:
                case casacore::FITS::DCOMPLEX:
                case casacore::FITS::VADESC:
                case casacore::FITS::NOVALUE:
                default:
                    break;
            }
        }

        fkw = fhi.kw.next(); // get next keyword
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

// ***** Computed entries *****

void FileExtInfoLoader::AddInitialComputedEntries(
    const std::string& hdu, CARTA::FileInfoExtended& extended_info, const std::string& filename, const std::vector<int>& render_axes) {
    // Add computed entries for filename, hdu, shape, and axes
    // Set name and HDU
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

    // Use header entries to determine computed entries
    std::string extname;
    casacore::IPosition shape;
    int chan_axis(-1), depth_axis(-1), stokes_axis(-1);
    std::vector<std::string> spectral_ctypes = {"ENER", "VOPT", "ZOPT", "VELO", "VRAD", "BETA"};

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
            std::transform(entry_value.begin(), entry_value.end(), entry_value.begin(), [](unsigned char c) { return std::toupper(c); });

            if (entry_value == "STOKES") {
                stokes_axis = axis_num;
            } else if ((entry_value.find("FREQ") == 0) || (entry_value.find("WAV") != std::string::npos) ||
                       (std::find(spectral_ctypes.begin(), spectral_ctypes.end(), entry_value) != spectral_ctypes.end())) {
                chan_axis = axis_num;
                if (chan_axis > 1) {
                    depth_axis = chan_axis;
                }
            }
        }
    }

    AddShapeEntries(extended_info, shape, chan_axis, depth_axis, stokes_axis, render_axes);
}

void FileExtInfoLoader::AddShapeEntries(CARTA::FileInfoExtended& extended_info, const casacore::IPosition& shape, int chan_axis,
    int depth_axis, int stokes_axis, const std::vector<int>& render_axes) {
    // Set fields/header entries for shape: dimensions, width, height, depth, stokes
    int num_dims(shape.size());
    int width(shape(render_axes[0]));
    int height(shape(render_axes[1]));
    int depth(depth_axis >= 0 ? shape(depth_axis) : 1);
    int stokes(stokes_axis >= 0 ? shape(stokes_axis) : 1);

    extended_info.set_dimensions(num_dims);
    extended_info.set_width(width);
    extended_info.set_height(height);
    extended_info.set_depth(depth);
    extended_info.set_stokes(stokes);

    // shape computed_entry
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
    auto shape_entry = extended_info.add_computed_entries();
    shape_entry->set_name("Shape");
    shape_entry->set_value(shape_string);
    shape_entry->set_entry_type(CARTA::EntryType::STRING);

    if (chan_axis >= 0) {
        // header entry for number of channels
        unsigned int nchan = shape(chan_axis);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(std::to_string(nchan));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nchan);
    }
    if (stokes_axis >= 0) {
        // header entry for number of stokes
        unsigned int nstokes = shape(stokes_axis);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Number of polarizations");
        entry->set_value(std::to_string(nstokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nstokes);
    }
}

void FileExtInfoLoader::AddComputedEntries(CARTA::FileInfoExtended& extended_info, casacore::ImageInterface<float>* image,
    const std::vector<int>& display_axes, casacore::String& radesys, bool use_image_for_entries) {
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
            std::string format_coords = fmt::format("[{}, {}]", coord1angle, coord2angle);
            // Add reference coords (angle format if possible)
            auto entry = extended_info.add_computed_entries();
            entry->set_name("Image reference coords");
            entry->set_value(format_coords);
            entry->set_entry_type(CARTA::EntryType::STRING);

            bool coord0IsDir(coord0.isConform("deg")), coord1IsDir(coord1.isConform("deg"));
            if (coord0IsDir || coord1IsDir) {
                // Reference coord(s) converted to deg
                std::string ref_coords_deg = ConvertCoordsToDeg(coord0, coord1);
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
        AddComputedEntriesFromHeaders(extended_info, display_axes);
    }

    casacore::ImageInfo image_info = image->imageInfo();
    if (image_info.hasBeam()) {
        const casacore::ImageBeamSet beam_set = image_info.getBeamSet();
        AddBeamEntry(extended_info, beam_set);
    }
}

void FileExtInfoLoader::AddComputedEntriesFromHeaders(CARTA::FileInfoExtended& extended_info, const std::vector<int>& display_axes) {
    // Convert display axis1 and axis2 header_entries into computed_entries;
    // For images with missing headers or headers which casacore/wcslib cannot process.
    // Axes are 1-based for header names (ctype, cunit, etc.), 0-based for display axes
    casacore::String suffix1(std::to_string(display_axes[0] + 1));
    casacore::String suffix2(std::to_string(display_axes[1] + 1));

    casacore::String ctype1, ctype2, cunit1("deg"), cunit2("deg"), frame, radesys, specsys, bunit;
    double min_double(std::numeric_limits<double>::min());
    double crval1(min_double), crval2(min_double), crpix1(min_double), crpix2(min_double), cdelt1(min_double), cdelt2(min_double);
    int velref(std::numeric_limits<int>::min());

    // Quit looking for key when have needed values
    bool need_ctype(true), need_crpix(true), need_crval(true), need_cdelt(true), need_frame(true), need_radesys(true);

    for (int i = 0; i < extended_info.header_entries_size(); ++i) {
        auto entry = extended_info.header_entries(i);
        auto entry_name = entry.name();

        // coordinate types
        if (need_ctype && (entry_name.find("CTYPE") == 0)) {
            if (entry_name.find("CTYPE" + suffix1) == 0) {
                ctype1 = entry.value();
                if (ctype1.contains("/")) {
                    ctype1 = ctype1.before("/");
                }
                ctype1.trim();
            } else if (entry_name.find("CTYPE" + suffix2) == 0) {
                ctype2 = entry.value();
                if (ctype2.contains("/")) {
                    ctype2 = ctype2.before("/");
                }
                ctype2.trim();
            }
            if (!ctype1.empty() && !ctype2.empty()) {
                need_ctype = false;
            }
        }

        // reference pixels
        if (need_crpix && (entry_name.find("CRPIX") == 0)) {
            if (entry_name.find("CRPIX" + suffix1) == 0) {
                crpix1 = entry.numeric_value();
            } else if (entry_name.find("CRPIX" + suffix2) == 0) {
                crpix2 = entry.numeric_value();
            }
            if ((crpix1 != min_double) && (crpix2 != min_double)) {
                need_crpix = false;
            }
        }

        // reference values
        if (need_crval && (entry_name.find("CRVAL") == 0)) {
            if (entry_name.find("CRVAL" + suffix1) == 0) {
                crval1 = entry.numeric_value();
            } else if (entry_name.find("CRVAL" + suffix2) == 0) {
                crval2 = entry.numeric_value();
            }
            if ((crval1 != min_double) && (crval2 != min_double)) {
                need_crval = false;
            }
        }

        // coordinate units
        if (entry_name.find("CUNIT") == 0) {
            if (entry_name.find("CUNIT" + suffix1) == 0) {
                cunit1 = entry.value();
                if (cunit1.contains("/")) {
                    cunit1 = cunit1.before("/");
                }
                cunit1.trim();
                if (cunit1.startsWith("DEG") || cunit1.startsWith("Deg")) { // Degrees, DEGREES nonstandard FITS values
                    cunit1 = "deg";
                }
            } else if (entry_name.find("CUNIT" + suffix2) == 0) {
                cunit2 = entry.value();
                if (cunit2.contains("/")) {
                    cunit2 = cunit2.before("/");
                }
                cunit2.trim();
                if (cunit2.startsWith("DEG") || cunit2.startsWith("Deg")) { // Degrees, DEGREES nonstandard FITS values
                    cunit2 = "deg";
                }
            }
        }

        // pixel increment
        if (need_cdelt && (entry_name.find("CDELT") == 0)) {
            if (entry_name.find("CDELT" + suffix1) == 0) {
                cdelt1 = entry.numeric_value();
            } else if (entry_name.find("CDELT" + suffix2) == 0) {
                cdelt2 = entry.numeric_value();
            }
            if ((cdelt1 != min_double) && (cdelt2 != min_double)) {
                need_cdelt = false;
            }
        }

        // Celestial frame
        if (need_frame && ((entry_name.find("EQUINOX") == 0) || (entry_name.find("EPOCH") == 0))) {
            need_frame = false;
            frame = entry.value();
            double numval = entry.numeric_value();
            if (frame.contains("2000") || (numval == 2000.0)) {
                frame = "J2000";
            } else if (frame.contains("1950") || (numval == 1950.0)) {
                frame = "B1950";
            }
        }

        // Radesys
        if (entry_name.find("RADESYS") == 0) {
            need_radesys = false;
            radesys = entry.value();
        }

        // Specsys
        if (entry_name.find("SPECSYS") == 0) {
            specsys = entry.value();
        }

        // Velocity definition
        if (entry_name.find("VELREF") == 0) {
            velref = entry.numeric_value();
        }

        // Bunit
        if (entry_name.find("BUNIT") == 0) {
            bunit = entry.value();
        }
    }

    // Set computed entries
    std::string coord_name1, coord_name2, projection;
    if (!need_ctype) {
        GetCoordNames(ctype1, ctype2, radesys, coord_name1, coord_name2, projection);
        std::string coord_type = fmt::format("{}, {}", coord_name1, coord_name2);
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
    if (!need_crpix) {
        std::string ref_pix = fmt::format("[{}, {}]", crpix1, crpix2);
        auto comp_entry = extended_info.add_computed_entries();
        comp_entry->set_name("Image reference pixels");
        comp_entry->set_value(ref_pix);
        comp_entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!need_crval) {
        // reference coordinates
        std::string format_coord1 = MakeAngleString(coord_name1, crval1, cunit1);
        std::string format_coord2 = MakeAngleString(coord_name2, crval2, cunit2);
        std::string ref_coords = fmt::format("[{}, {}]", format_coord1, format_coord2);
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Image reference coords");
        entry->set_value(ref_coords);
        entry->set_entry_type(CARTA::EntryType::STRING);

        // reference coordinates in deg
        casacore::Quantity q1(crval1, cunit1);
        casacore::Quantity q2(crval2, cunit2);
        bool q1IsDir(q1.isConform("deg")), q2IsDir(q2.isConform("deg"));
        if (q1IsDir || q2IsDir) {
            // Reference coord(s) converted to deg
            std::string ref_coords_deg = ConvertCoordsToDeg(q1, q2);
            auto comp_entry = extended_info.add_computed_entries();
            comp_entry->set_name("Image ref coords (deg)");
            comp_entry->set_value(ref_coords_deg);
            comp_entry->set_entry_type(CARTA::EntryType::STRING);
        }
    }

    if (!need_cdelt) {
        // Increment in arcsec
        casacore::Quantity inc1(cdelt1, cunit1);
        casacore::Quantity inc2(cdelt2, cunit2);
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

    if (!need_radesys || !need_frame) {
        std::string direction_frame;
        if (need_radesys) {
            if (frame == "J2000") {
                direction_frame = "FK5, J2000";
            } else if (frame == "B1950") {
                direction_frame = "FK4, B1950";
            } else if (frame == "ICRS") {
                direction_frame = "ICRS, J2000";
            } else {
                direction_frame = frame;
            }
        } else if (need_frame) {
            direction_frame = radesys;
        } else {
            direction_frame = fmt::format("{}, {}", radesys, frame);
        }
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(direction_frame);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!specsys.empty()) {
        auto entry = extended_info.add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(specsys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (velref > 0) {
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
            gaussian_beam.getPA("deg").getValue());

        auto entry = extended_info.add_computed_entries();
        entry->set_name(entry_name);
        entry->set_entry_type(CARTA::EntryType::STRING);
        entry->set_value(beam_info);
    }
}

// ***** FITS keyword conversion *****

bool FileExtInfoLoader::GetFitsKwList(casacore::FitsInput& fits_input, unsigned int hdu, casacore::FitsKeywordList& kwlist) {
    // Use casacore HeaderDataUnit to get keyword list
    if (hdu == 0) {
        switch (fits_input.datatype()) {
            case casacore::FITS::FLOAT: {
                casacore::PrimaryArray<casacore::Float> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::DOUBLE: {
                casacore::PrimaryArray<casacore::Double> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::SHORT: {
                casacore::PrimaryArray<casacore::Short> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::LONG: {
                casacore::PrimaryArray<casacore::Int> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::BYTE: {
                casacore::PrimaryArray<casacore::uChar> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            default:
                return false;
        }
    } else {
        switch (fits_input.datatype()) {
            case casacore::FITS::FLOAT: {
                casacore::ImageExtension<casacore::Float> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::DOUBLE: {
                casacore::ImageExtension<casacore::Double> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::SHORT: {
                casacore::ImageExtension<casacore::Short> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::LONG: {
                casacore::ImageExtension<casacore::Int> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            case casacore::FITS::BYTE: {
                casacore::ImageExtension<casacore::uChar> fits_image(fits_input);
                kwlist = fits_image.kwlist();
                break;
            }
            default:
                return false;
        }
    }
    return true;
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
    casacore::MVAngle mva(quant1);
    return mva.string(format, 10);
}

std::string FileExtInfoLoader::ConvertCoordsToDeg(const casacore::Quantity& coord0, const casacore::Quantity& coord1) {
    // If possible, convert quantities to degrees. Return formatted string
    casacore::Quantity coord0_deg(coord0), coord1_deg(coord1);
    if (coord0.isConform("deg")) {
        coord0_deg = coord0.get("deg");
    }
    if (coord1.isConform("deg")) {
        coord1_deg = coord1.get("deg");
    }

    return fmt::format("[{}, {}]", coord0_deg, coord1_deg);
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

void FileExtInfoLoader::GetCoordNames(std::string& ctype1, std::string& ctype2, std::string& radesys, std::string& coord_name1,
    std::string& coord_name2, std::string& projection) {
    // split ctype1 and ctype2 into type and projection
    std::unordered_map<std::string, std::string> names = {
        {"RA", "Right Ascension"}, {"DEC", "Declination"}, {"GLON", "Longitude"}, {"GLAT", "Latitude"}};

    auto delim_pos = ctype1.find("--");
    if (delim_pos != std::string::npos) {
        coord_name1 = ctype1.substr(0, delim_pos);
        projection = ctype1.substr(delim_pos, std::string::npos);
        auto proj_start = projection.find_first_not_of('-');
        projection = projection.substr(proj_start, std::string::npos);
    } else {
        coord_name1 = ctype1;
    }

    if (radesys.empty() && (coord_name1 == "GLON")) {
        radesys = "GALACTIC";
    }

    if (names.count(coord_name1)) {
        coord_name1 = names[coord_name1];
    }

    delim_pos = ctype2.find("--");
    if (delim_pos != std::string::npos) {
        // split ctype2
        coord_name2 = ctype2.substr(0, delim_pos);
    } else {
        coord_name2 = ctype2;
    }

    if (names.count(coord_name2)) {
        coord_name2 = names[coord_name2];
    }
}

int FileExtInfoLoader::GetFitsBitpix(casacore::ImageInterface<float>* image) {
    // Use FITS data type to set bitpix
    int bitpix(-32);
    casacore::DataType data_type(casacore::DataType::TpFloat);

    if (image->imageType() == "FITSImage") {
        casacore::FITSImage* fits_image = dynamic_cast<casacore::FITSImage*>(image);
        if (fits_image) {
            data_type = fits_image->internalDataType();
        }
    } else {
        carta::CartaFitsImage* fits_image = dynamic_cast<carta::CartaFitsImage*>(image);
        if (fits_image) {
            data_type = fits_image->internalDataType();
        }
    }

    switch (data_type) {
        case casacore::DataType::TpUChar:
            bitpix = 8;
            break;
        case casacore::DataType::TpShort:
            bitpix = 16;
            break;
        case casacore::DataType::TpInt:
            bitpix = 32;
            break;
        case casacore::DataType::TpInt64:
            bitpix = 64;
            break;
        case casacore::DataType::TpFloat:
            bitpix = -32;
            break;
        case casacore::DataType::TpDouble:
            bitpix = -64;
            break;
        default:
            bitpix = -32;
            break;
    }

    return bitpix;
}
