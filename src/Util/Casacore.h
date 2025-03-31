/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_CASACORE_H_
#define CARTA_SRC_UTIL_CASACORE_H_

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

/**
 * @brief Validates and resolves folder paths, ensuring the starting directory is within the top-level directory.
 *
 * This function checks and resolves the given top-level and starting directories.
 * If default placeholder values ("base" or "root") are found, they are replaced accordingly.
 * The function verifies that both directories exist and are accessible, and ensures that
 * the starting directory is a valid subdirectory of the top-level directory.
 *
 * @param[in,out] top_level_string Reference to the top-level directory path.
 *                                  It is updated to its resolved absolute path.
 * @param[in,out] starting_string Reference to the starting directory path.
 *                                  It is updated to its resolved absolute path.
 *
 * @return `true` if the paths are valid and the starting directory is within the top-level directory,
 *         otherwise `false`.
 *
 * @note If `starting_string` is invalid, it is replaced with `top_level_string`.
 * @note If `starting_string` is not a subdirectory of `top_level_string`, the function logs a critical error and returns `false`.
 *
 * @warning If both `top_level_string` and `starting_string` are set to their default placeholders ("base" and "root"),
 *          the function logs a critical error and returns `false`.
 */
bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);

/**
 * @brief Determines whether a given folder is a subdirectory of a specified top-level folder.
 *
 * This function checks if `folder` is a subdirectory of `top_folder` by
 * resolving both paths to their absolute, weakly canonical forms and then
 * traversing up the directory hierarchy.
 *
 * @param[in] folder The folder path to check, represented as a string.
 * @param[in] top_folder The top-level folder path to compare against, represented as a string.
 *
 * @return `true` if `folder` is a subdirectory of `top_folder` or the same directory,
 *         `false` otherwise.
 *
 * @warning If `top_folder` is empty, the function will always return `true`.
 */
bool IsSubdirectory(std::string folder, std::string top_folder);

/**
 * @brief Resolves a file path based on a given root directory and relative subdirectory.
 *
 * This function constructs an absolute file path using a specified root directory,
 * a relative subdirectory, and a file name. It checks whether the resulting file path
 * exists and is readable. If any issue is encountered, an error message is set.
 *
 * @param[in] root_dir A string refence to the root directory in which the file is expected to reside.
 * @param[in] directory A string refence to the relative directory path within the root directory.
 * @param[in] file A string refence to the name of the file to resolve.
 * @param[out] message A reference to a string that will contain an error message if the resolution fails.
 *
 * @return The resolved absolute filename as a `casacore::String` if successful, otherwise an empty string.
 */
casacore::String GetResolvedFilename(
    const std::string& root_dir, const std::string& directory, const std::string& file, std::string& message);

/**
 * @brief Determines the image type of a given filename using Casacore.
 *
 * This function utilizes `casacore::ImageOpener::imageType` to identify
 * the type of an image file based on its name.
 *
 * @param[in] filename A string reference to the path to the image file..
 *
 * @return The image type as a `casacore::ImageOpener::ImageTypes` value.
 */
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

/**
 * @brief Determines the spectral coordinate preferences based on an image's native spectral type.
 *
 * This function analyses the spectral coordinate system of the provided `image` and sets
 * preference flags for velocity, wavelength, and their specific variations. It considers
 * the image's native spectral type and applies special handling for `CartaMiriadImage` types.
 *
 * @param[in] image Pointer to a `casacore::ImageInterface<float>` representing the image whose
 *                  spectral coordinate system will be analyzed.
 * @param[out] prefer_velocity A boolean value set to `true` if velocity-based representation is preferred.
 * @param[out] optical_velocity A boolean value set to `true` if optical velocity representation is preferred.
 * @param[out] prefer_wavelength A boolean value set to `true` if wavelength-based representation is preferred.
 * @param[out] air_wavelength A boolean value set to `true` if air wavelength representation is preferred.
 *
 * @note If the image contains a spectral axis, its native type is determined and used to
 *       update the preference flags accordingly.
 *
 * @warning The function modifies the output parameters in place; ensure they are initialized properly.
 */
void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

/**
 * @brief Formats a Gaussian beam's parameters into a human-readable string.
 *
 * This function retrieves the major axis, minor axis, and position angle (PA) of
 * a given `casacore::GaussianBeam` and formats them into a structured string with
 * six decimal places of precision.
 *
 * @param[in] gaussian_beam The reference to the Casacore Gaussian beam object whose
 *         parameters will be formatted.
 *
 * @return A formatted string containing the beam's major axis, minor axis, and
 *         position angle, including their respective units.
 *
 * @note The output format is:
 *       `"major: <value> <unit> minor: <value> <unit> pa: <value> <unit>"`
 */
std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam);

/**
 * @brief Formats a Casacore quantity into a human-readable string.
 *
 * This function converts a `casacore::Quantity` into a string representation
 * with six decimal places of precision, including its unit.
 *
 * @param[in] quantity A reference to a Casacore Quantity object that represents
 *         the quantity to be formatted.
 *
 * @return A formatted string in the form of `"<value> <unit>"`, where `<value>`
 *         is displayed with six decimal places.
 */
std::string FormatQuantity(const casacore::Quantity& quantity);

/**
 * @brief Normalizes a given unit string to conform to standard Casacore unit conventions (case-sensitive).
 *
 * This function converts various unit representations into their standardised Casacore
 * equivalents. It replaces non-standard units with correct forms, fixes case inconsistencies,
 * and removes invalid characters. Additionally, it attempts to map the unit to a valid
 * Casacore unit using `UnitMap::fromFITS` and `UnitVal::check`.
 *
 * @param[in,out] unit A string reference to the unit string to normalise. It is modified in place with
 *       the corrected and validated unit name if possible.
 *
 * @note If the unit contains a recognized prefix, the function attempts to normalise with and without the prefix.
 *
 * @warning If the unit cannot be resolved to a known Casacore unit, it remains unchanged.
 *
 * @exception casacore::AipsError Caught internally when Casacore unit conversions fail.
 */
void NormalizeUnit(casacore::String& unit);

/**
 * @brief Parses an AIPS-style beam header to extract beam parameters with regex.
 *
 * This function uses regular expressions (regex) to extract the major axis (BMAJ),
 * minor axis (BMIN), and position angle (BPA) from an AIPS-style history
 * beam header string. It handles two common formats: one using "Beam ="
 * notation and another using "BMAJ=", "BMIN=", and "BPA=" notation.
 *
 * @param[in] header A string reference to the history beam header string to be parsed.
 * @param[out] bmaj A string reference to the extracted major axis value with its unit.
 * @param[out] bmin A string reference to the extracted minor axis value with its unit.
 * @param[out] bpa A string reference to the extracted position angle value with its unit.
 *
 * @return `true` if the header was successfully parsed and values were extracted,
 *         otherwise `false`.
 *
 * @note Units are normalized to "deg" when "degrees" is found in the header.
 *
 * @warning If the header format does not match expected patterns, the function
 *          logs a debug message and returns `false`.
 */
bool ParseHistoryBeamHeader(std::string& header, std::string& bmaj, std::string& bmin, std::string& bpa);

#endif // CARTA_SRC_UTIL_CASACORE_H_
