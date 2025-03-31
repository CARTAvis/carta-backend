/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Casacore.h"

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

bool CheckFolderPaths(string& top_level_string, string& starting_string) {
    if (top_level_string == "base" && starting_string == "root") {
        spdlog::critical("Must set top level or starting directory. Exiting carta.");

        return false;
    }

    if (top_level_string == "base")

        top_level_string = starting_string;

    if (starting_string == "root")

        starting_string = top_level_string;

    // Check if the top-level directory exists and is accessible

    fs::path top_level_path = fs::weakly_canonical(fs::absolute(fs::path(top_level_string)));

    if (!fs::exists(top_level_path) || !fs::is_directory(top_level_path)) {
        spdlog::critical("Invalid top level directory, does not exist or is not a readable directory. Exiting carta.");

        return false;
    }

    top_level_string = top_level_path.string();

    // Check if the starting directory exists and is accessible

    fs::path starting_path = fs::weakly_canonical(fs::absolute(fs::path(starting_string)));

    if (!fs::exists(starting_path) || !fs::is_directory(starting_path)) {
        spdlog::warn("Invalid starting directory, using the provided top level directory instead.");

        starting_string = top_level_string;

    } else {
        starting_string = starting_path.string();
    }

    // Check if starting directory is a subdirectory of the top-level directory

    if (!IsSubdirectory(starting_path, top_level_path)) {
        spdlog::critical("Starting {} must be a subdirectory of top level {}. Exiting carta.", starting_string, top_level_string);

        return false;
    }

    return true;
}

bool IsSubdirectory(string folder, string top_folder) {
    auto folder_path = fs::weakly_canonical(fs::absolute(folder));

    auto parent_path = fs::weakly_canonical(fs::absolute(top_folder));

    if (parent_path.empty() || folder_path == parent_path) {
        return true;
    }

    auto current_path = folder_path;

    while (current_path.has_parent_path()) {
        current_path = current_path.parent_path();

        if (current_path == parent_path) {
            return true;
        }

        if (current_path == fs::path("/")) {
            break;
        }
    }

    return false;
}

casacore::String GetResolvedFilename(const string& root_dir, const string& directory, const string& file, string& message) {
    // Given directory (relative to root directory) and file, return resolved file path.
    // Check if file path exists and is readable.
    casacore::String resolved_filename;
    casacore::Path path(root_dir);
    path.append(directory);
    casacore::File cc_file(path);

    // Check directory
    if (!cc_file.exists()) {
        message = "Directory " + directory + " does not exist.";
    } else if (!cc_file.isReadable()) {
        message = "Directory " + directory + " is not readable.";
    } else {
        // Check file
        path.append(file);
        cc_file = casacore::File(path);
        if (!cc_file.exists()) {
            message = "File " + file + " does not exist.";
        } else if (!cc_file.isReadable()) {
            message = "File " + file + " is not readable.";
        } else {
            try {
                resolved_filename = path.resolvedName();
            } catch (const casacore::AipsError& err) {
                // resolvedName() calls absoluteName(), which returns an empty path due to parsing bug to remove dots (., ..) from path,
                // resulting in AipsError. Workaround just sets expanded name used for exists() and isReadable().
                if (path.absoluteName().empty()) {
                    resolved_filename = path.expandedName();
                } else {
                    message = err.getMesg();
                }
            }
        }
    }

    return resolved_filename;
}

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength) {
    prefer_velocity = optical_velocity = prefer_wavelength = air_wavelength = false;
    casacore::CoordinateSystem coord_sys(image->coordinates());
    if (coord_sys.hasSpectralAxis()) { // prefer spectral axis native type
        casacore::SpectralCoordinate::SpecType native_type;
        if (image->imageType() == "CartaMiriadImage") { // workaround to get correct native type
            carta::CartaMiriadImage* miriad_image = static_cast<carta::CartaMiriadImage*>(image);
            native_type = miriad_image->NativeType();
        } else {
            native_type = coord_sys.spectralCoordinate().nativeType();
        }
        switch (native_type) {
            case casacore::SpectralCoordinate::FREQ: {
                break;
            }
            case casacore::SpectralCoordinate::VRAD:
            case casacore::SpectralCoordinate::BETA: {
                prefer_velocity = true;
                break;
            }
            case casacore::SpectralCoordinate::VOPT: {
                prefer_velocity = true;

                // Check doppler type; oddly, native type can be VOPT but doppler is RADIO--?
                casacore::MDoppler::Types vel_doppler(coord_sys.spectralCoordinate().velocityDoppler());
                if ((vel_doppler == casacore::MDoppler::Z) || (vel_doppler == casacore::MDoppler::OPTICAL)) {
                    optical_velocity = true;
                }
                break;
            }
            case casacore::SpectralCoordinate::WAVE: {
                prefer_wavelength = true;
                break;
            }
            case casacore::SpectralCoordinate::AWAV: {
                prefer_wavelength = true;
                air_wavelength = true;
                break;
            }
        }
    }
}

std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam) {
    std::string result;
    result += fmt::format("major: {:.6f} {} ", gaussian_beam.getMajor().getValue(), gaussian_beam.getMajor().getUnit());
    result += fmt::format("minor: {:.6f} {} ", gaussian_beam.getMinor().getValue(), gaussian_beam.getMinor().getUnit());
    result += fmt::format("pa: {:.6f} {}", gaussian_beam.getPA().getValue(), gaussian_beam.getPA().getUnit());
    return result;
}

std::string FormatQuantity(const casacore::Quantity& quantity) {
    return fmt::format("{:.6f} {}", quantity.getValue(), quantity.getUnit());
}

void NormalizeUnit(casacore::String& unit) {
    // Convert unit string to "proper" units according to casacore
    // Fix nonstandard units which pass check
    unit.gsub("JY", "Jy");
    unit.gsub("jy", "Jy");
    unit.gsub("Beam", "beam");
    unit.gsub("BEAM", "beam");
    unit.gsub("Jypb", "Jy/beam");
    unit.gsub("JyPB", "Jy/beam");
    unit.gsub("Jy beam-1", "Jy/beam");
    unit.gsub("Jy beam^-1", "Jy/beam");
    unit.gsub("beam-1 Jy", "Jy/beam");
    unit.gsub("beam^-1 Jy", "Jy/beam");
    unit.gsub("Pixel", "pixel");
    unit.gsub("DEGREE", "deg");
    unit.gsub("\"", "");

    // Convert unit without prefix
    try {
        // Convert upper to mixed/lower case if needed
        auto normalized_unit = casacore::UnitMap::fromFITS(unit).getName();

        if (casacore::UnitVal::check(normalized_unit)) {
            unit = normalized_unit;
            return;
        }
    } catch (const casacore::AipsError& err) {
        // check() should catch the error and return false, but does not
    }

    // Convert unit with (possible) prefix
    casacore::String prefix(unit[0]);
    casacore::UnitName unit_name;
    if (casacore::UnitMap::getPref(prefix, unit_name)) {
        try {
            // Convert unit with "prefix" removed
            casacore::String unit_no_prefix = unit.substr(1);
            unit_no_prefix.upcase();
            auto normalized_unit = casacore::UnitMap::fromFITS(unit_no_prefix).getName();

            if (casacore::UnitVal::check(normalized_unit)) {
                unit = prefix + normalized_unit;
                return;
            }
        } catch (const casacore::AipsError& err) {
            // not caught by check()
        }
    }

    // Convert uppercase unit without prefix, else return unknown input unit
    casacore::String up_unit(unit);
    up_unit.upcase();
    try {
        // Convert upper to mixed/lower case
        auto normalized_unit = casacore::UnitMap::fromFITS(up_unit).getName();

        if (casacore::UnitVal::check(normalized_unit)) {
            unit = normalized_unit;
        }
    } catch (const casacore::AipsError& err) {
        // not caught by check()
    }
}

bool ParseHistoryBeamHeader(std::string& header, std::string& bmaj, std::string& bmin, std::string& bpa) {
    // Parse AIPS beam header using regex_match.
    // Returns false if regex failed, else true with beam value-unit strings.
    std::regex r;
    std::cmatch results;
    bool matched(false);

    if (header.find("Beam") != std::string::npos) {
        // Example:
        // HISTORY RESTOR Beam =  2.000E+00 x  1.800E+00 arcsec, pa =  8.000E+01 degrees
        r = R"/(.*Beam\s*=\s*([\d.Ee+-]+)\s*x\s*([\d.Ee+-]+)\s*([A-Za-z]*)\s*,*\s*pa\s*=\s*([\d.Ee+-]+)\s*([A-Za-z]*).*)/";
        matched = std::regex_match(header.c_str(), results, r);
    } else if (header.find("BMAJ") != std::string::npos) {
        // Examples:
        // HISTORY CONVL BMAJ=  5.0000 BMIN=  5.0000 BPA=   0.0/Output beam
        // HISTORY AIPS   CLEAN BMAJ=  1.3889E-03 BMIN=  1.3889E-03 BPA=   0.00
        r = R"/(.*BMAJ\s*=\s*([\d.Ee+-]+)\s*BMIN\s*=\s*([\d.Ee+-]+)\s*BPA\s*=\s*([\d.Ee+-]+).*)/";
        matched = std::regex_match(header.c_str(), results, r);
    }

    if (matched) {
        if (results.size() == 4) {
            // 0 matched expr, 1 bmaj, 2 bmin, 3 bpa. Use default unit.
            bmaj = results.str(1) + "deg";
            bmin = results.str(2) + "deg";
            bpa = results.str(3) + "deg";
        } else if (results.size() == 6) {
            // 0 matched expr, 1 bmaj, 2 bmin, 3 unit, 4 bpa, 5 unit
            auto unit3 = results.str(3);
            auto unit5 = results.str(5);
            bmaj = results.str(1) + (unit3 == "degrees" ? "deg" : unit3);
            bmin = results.str(2) + (unit3 == "degrees" ? "deg" : unit3);
            bpa = results.str(4) + (unit5 == "degrees" ? "deg" : unit5);
        } else {
            spdlog::debug("Unable to set history beam header {}: unexpected format.", header);
            matched = false;
        }
    }

    return matched;
}
