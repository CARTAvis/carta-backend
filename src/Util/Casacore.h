/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_CASACORE_H_
#define CARTA_SRC_UTIL_CASACORE_H_

#include <carta-protobuf/enums.pb.h>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

/**
 * @brief Resolves a file path based on a given root directory and relative subdirectory.
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
 * @brief Determines the image type of a directory.  Returns unknown for all files.
 *
 * @param[in] folder_path The folder path to check, represented as a string.
 * @param[out] message Error message if an image but not a supported type
 *
 * @return The CARTA::FileType of the directory, unknown if not an image.
 */
CARTA::FileType FolderImageType(const std::string& folder_path, std::string& message);

/**
 * @brief Determines the spectral coordinate preferences based on an image's native spectral type.
 *
 * @param[in] image Pointer to a `casacore::ImageInterface<float>` representing the image whose
 *                  spectral coordinate system will be analyzed.
 * @param[out] prefer_velocity A boolean value set to `true` if velocity-based representation is preferred.
 * @param[out] optical_velocity A boolean value set to `true` if optical velocity representation is preferred.
 * @param[out] prefer_wavelength A boolean value set to `true` if wavelength-based representation is preferred.
 * @param[out] air_wavelength A boolean value set to `true` if air wavelength representation is preferred.
 */
void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

/**
 * @brief Formats a Gaussian beam's parameters into a human-readable string.
 *
 * @param[in] gaussian_beam The reference to the Casacore Gaussian beam object whose
 *         parameters will be formatted.
 *
 * @return A formatted string containing the beam's major axis, minor axis, and
 *         position angle, including their respective units.
 */
std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam);

/**
 * @brief Formats a Casacore quantity into a human-readable string.
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
 * @param[in,out] unit A string reference to the unit string to normalise. It is modified in place with
 *       the corrected and validated unit name if possible.
 */
void NormalizeUnit(casacore::String& unit);

/**
 * @brief Check if input unit string is in the format used by the GILDAS CLASS software.
 *
 * @param[in] unit A unit string to check.
 *
 * @return `true` if a GILDAS unit.
 */
bool IsGildasUnit(const casacore::String& unit);

#endif // CARTA_SRC_UTIL_CASACORE_H_
