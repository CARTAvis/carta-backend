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

/**
 * @details This function checks and resolves the given top-level and starting directories.
 * If default placeholder values ("base" or "root") are found, they are replaced accordingly.
 * The function verifies that both directories exist and are accessible, and ensures that
 * the starting directory is a valid subdirectory of the top-level directory.
 *
 * @note If `starting_string` is invalid, it is replaced with `top_level_string`.
 * @note If `starting_string` is not a subdirectory of `top_level_string`, the function logs a critical error and returns `false`.
 *
 * @warning If both `top_level_string` and `starting_string` are set to their default placeholders ("base" and "root"),
 *          the function logs a critical error and returns `false`.
 */
bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string) {
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

/**
 * @details This function checks if `folder` is a subdirectory of `top_folder` by
 * resolving both paths to their absolute, weakly canonical forms and then
 * traversing up the directory hierarchy.
 *
 * @warning If `top_folder` is empty, the function will always return `true`.
 */
bool IsSubdirectory(std::string folder, std::string top_folder) {
    folder = casacore::Path(folder).absoluteName();
    top_folder = casacore::Path(top_folder).absoluteName();
    if (top_folder.empty()) {
        return true;
    }
    if (folder == top_folder) {
        return true;
    }
    casacore::Path folder_path(folder);
    std::string parent_string(folder_path.dirName());
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

/**
 * @details This function constructs an absolute file path using a specified root directory,
 * a relative subdirectory, and a file name. It checks whether the resulting file path
 * exists and is readable. If any issue is encountered, an error message is set.
 */
casacore::String GetResolvedFilename(
    const std::string& root_dir, const std::string& directory, const std::string& file, std::string& message) {
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

/**
 * @details This function analyses the spectral coordinate system of the provided `image` and sets
 * preference flags for velocity, wavelength, and their specific variations. It considers
 * the image's native spectral type and applies special handling for `CartaMiriadImage` types.
 *
 * @note If the image contains a spectral axis, its native type is determined and used to
 *       update the preference flags accordingly.
 *
 * @warning The function modifies the output parameters in place; ensure they are initialized properly.
 */
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

/**
 * @details This function retrieves the major axis, minor axis, and position angle (PA) of
 * a given `casacore::GaussianBeam` and formats them into a structured string with
 * six decimal places of precision.
 *
 * @note The output format is:
 *      `"major: <value> <unit> minor: <value> <unit> pa: <value> <unit>"`
 */
std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam) {
    std::string result;
    result += fmt::format("major: {:.6f} {} ", gaussian_beam.getMajor().getValue(), gaussian_beam.getMajor().getUnit());
    result += fmt::format("minor: {:.6f} {} ", gaussian_beam.getMinor().getValue(), gaussian_beam.getMinor().getUnit());
    result += fmt::format("pa: {:.6f} {}", gaussian_beam.getPA().getValue(), gaussian_beam.getPA().getUnit());
    return result;
}

/**
 * @details This function converts a `casacore::Quantity` into a string representation
 * with six decimal places of precision, including its unit.
 */
std::string FormatQuantity(const casacore::Quantity& quantity) {
    return fmt::format("{:.6f} {}", quantity.getValue(), quantity.getUnit());
}

/**
 * @details This function converts various unit representations into their standardised Casacore
 * equivalents. It replaces non-standard units with correct forms, fixes case inconsistencies,
 * and removes invalid characters. Additionally, it attempts to map the unit to a valid
 * Casacore unit using `UnitMap::fromFITS` and `UnitVal::check`.
 *
 * @note If the unit contains a recognized prefix, the function attempts to normalise with and without the prefix.
 *
 * @warning If the unit cannot be resolved to a known Casacore unit, it remains unchanged.
 *
 * @exception casacore::AipsError Caught internally when Casacore unit conversions fail.
 */
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

/**
 * @details This function uses regular expressions (regex) to extract the major axis (BMAJ),
 * minor axis (BMIN), and position angle (BPA) from an AIPS-style history
 * beam header string. It handles two common formats: one using "Beam ="
 * notation and another using "BMAJ=", "BMIN=", and "BPA=" notation.
 *
 * @note Units are normalized to "deg" when "degrees" is found in the header.
 *
 * @warning If the header format does not match expected patterns, the function
 *          logs a debug message and returns `false`.
 */
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
