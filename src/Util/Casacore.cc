/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Casacore.h"

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

bool CheckFolderPaths(string& top_level_string, string& starting_string) {
    // TODO: is this code needed at all? Was it a weird workaround?
    {
        if (top_level_string == "base" && starting_string == "root") {
            spdlog::critical("Must set top level or starting directory. Exiting carta.");
            return false;
        }
        if (top_level_string == "base")
            top_level_string = starting_string;
        if (starting_string == "root")
            starting_string = top_level_string;
    }
    // TODO: Migrate to std::filesystem
    // check top level
    casacore::File top_level_folder(top_level_string);
    if (!(top_level_folder.exists() && top_level_folder.isDirectory(true) && top_level_folder.isReadable() &&
            top_level_folder.isExecutable())) {
        spdlog::critical("Invalid top level directory, does not exist or is not a readable directory. Exiting carta.");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        top_level_string = top_level_folder.path().resolvedName(); // fails on top level folder /
    } catch (casacore::AipsError& err) {
        try {
            top_level_string = top_level_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            spdlog::error(err.getMesg());
        }
        if (top_level_string.empty())
            top_level_string = "/";
    }
    // check starting folder
    casacore::File starting_folder(starting_string);
    if (!(starting_folder.exists() && starting_folder.isDirectory(true) && starting_folder.isReadable() &&
            starting_folder.isExecutable())) {
        spdlog::warn("Invalid starting directory, using the provided top level directory instead.");
        starting_string = top_level_string;
    } else {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        try {
            starting_string = starting_folder.path().resolvedName(); // fails on top level folder /
        } catch (casacore::AipsError& err) {
            try {
                starting_string = starting_folder.path().absoluteName();
            } catch (casacore::AipsError& err) {
                spdlog::error(err.getMesg());
            }
            if (starting_string.empty())
                starting_string = "/";
        }
    }
    bool is_subdirectory = IsSubdirectory(starting_string, top_level_string);
    if (!is_subdirectory) {
        spdlog::critical("Starting {} must be a subdirectory of top level {}. Exiting carta.", starting_string, top_level_string);
        return false;
    }
    return true;
}

bool IsSubdirectory(string folder, string top_folder) {
    folder = casacore::Path(folder).absoluteName();
    top_folder = casacore::Path(top_folder).absoluteName();
    if (top_folder.empty()) {
        return true;
    }
    if (folder == top_folder) {
        return true;
    }
    casacore::Path folder_path(folder);
    string parent_string(folder_path.dirName());
    if (parent_string == top_folder) {
        return true;
    }
    while (parent_string != top_folder) { // navigate up directory tree
        folder_path = casacore::Path(parent_string);
        parent_string = folder_path.dirName();
        if (parent_string == top_folder) {
            return true;
        } else if (parent_string == "/") {
            break;
        }
    }
    return false;
}

casacore::String GetResolvedFilename(const string& root_dir, const string& directory, const string& file) {
    // Given directory (relative to root folder) and file, return resolved path and filename
    // (absolute pathname with symlinks resolved)
    casacore::String resolved_filename;
    casacore::Path root_path(root_dir);
    root_path.append(directory);
    root_path.append(file);
    casacore::File cc_file(root_path);
    if (cc_file.exists()) {
        try {
            resolved_filename = cc_file.path().resolvedName();
        } catch (casacore::AipsError& err) {
            // return empty string
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
