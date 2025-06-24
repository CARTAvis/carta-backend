/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Casacore.h"

#include <regex>

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

const std::regex GILDAS_REGEX(" *[a-zA-Z]+[ .]+\\(T[a-zA-Z_]+[*.]*\\) *");

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
 * @details This function determines the image type of a directory.  If not an image, returns UNKNOWN type.
 *
 * @note If the path is a file, does not check type and returns UNKNOWN.
 */
CARTA::FileType FolderImageType(const std::string& folder_path, std::string& message) {
    // Return CARTA::FileType enum for input folder (image type only for folder only).
    // Returns UNKNOWN for files (including image files), unsupported image types, and plain directories.
    // Return parameter `message` is set for unsupported image types.
    CARTA::FileType carta_type(CARTA::FileType::UNKNOWN);
    casacore::File input_file(folder_path);
    if (input_file.isRegular()) {
        return carta_type;
    }

    switch (CasacoreImageType(folder_path)) {
        case casacore::ImageOpener::AIPSPP:
        case casacore::ImageOpener::IMAGECONCAT:
        case casacore::ImageOpener::IMAGEEXPR:
        case casacore::ImageOpener::COMPLISTIMAGE: {
            carta_type = CARTA::FileType::CASA;
            break;
        }
        case casacore::ImageOpener::MIRIAD: {
            carta_type = CARTA::FileType::MIRIAD;
            break;
        }
        case casacore::ImageOpener::GIPSY:
        case casacore::ImageOpener::CAIPS:
        case casacore::ImageOpener::NEWSTAR: {
            message = fmt::format("{}: image type not supported", folder_path);
            break;
        }
        default: {
            break;
        }
    }

    return carta_type;
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
    if (IsGildasUnit(unit)) {
        return; // do not casacore-ize unit
    }

    unit.gsub("JY", "Jy");
    unit.gsub("jy", "Jy");
    unit.gsub("Beam", "beam");
    unit.gsub("BEAM", "beam");
    unit.gsub("Jypb", "Jy/beam");
    unit.gsub("JyPB", "Jy/beam");
    unit.gsub("Jy beam-1", "Jy/beam");
    unit.gsub("Jy beam^-1", "Jy/beam");
    unit.gsub("beam-1 Jy", "Jy/beam");
    unit.gsub("beam-1.Jy", "Jy/beam");
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
 * @details This function uses regex to check if unit is in the GILDAS CLASS software format
 * "unit (Ttype)" where the "type" describes the temperature T.
 * It also tests for casacore Unit name changes where " " and "*" are replaced with ".".
 * For example: "K (Ta*)" -->  "K.(Ta.)" in casacore.
 */
bool IsGildasUnit(const casacore::String& unit) {
    return std::regex_match(unit, GILDAS_REGEX);
}
