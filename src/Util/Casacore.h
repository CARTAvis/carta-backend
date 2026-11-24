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
#include <casacore/lattices/LEL/LatticeExprNode.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

/**
 * @brief Resolves a file path based on a given root directory and relative subdirectory.
 *
 * @param[in] root_dir A string refence to the root directory in which the file is expected to reside.
 * @param[in] directory A string refence to the relative directory path within the root directory.
 * @param[in] file A string refence to the name of the file to resolve.
 * @param[out] message A reference to a string that will contain an error message if the resolution fails.
 * @param[out] checked_dir If given, set on success to the directory as GetPathInFolder() returned it, before symlinks are followed.
 *
 * @return The resolved absolute filename as a `casacore::String` if successful, otherwise an empty string.
 */
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file,
    std::string& message, std::string* checked_dir = nullptr);

/**
 * @brief Checks that a path is the given folder or inside it.
 *
 * The check works on the text of the path, so symlinks inside the folder can still point anywhere. Open the returned path,
 * not the input: in the text link/.. is just the folder, but the OS would follow link first and go up from where it points.
 * To check where symlinks lead instead, give it paths that have had their symlinks followed.
 *
 * @param[in] path The path to check.
 * @param[in] folder The folder the path must stay inside.
 * @param[in] expand Whether to expand environment variables in the path first, as casacore will if it is given the path.
 *
 * @return The path with . and .. removed, or an empty string if it is outside the folder.
 */
std::string GetPathInFolder(const std::string& path, const std::string& folder, bool expand = true);

/**
 * @brief Checks that the images named in an image expression are inside the given folder.
 *
 * casacore opens the images in an expression itself, so the names are found here the way its lexer finds them and checked
 * before it parses the expression. Nothing is checked if the folder is /.
 *
 * @param[in] expression The image expression.
 * @param[in] directory The directory that names in the expression are relative to, as GetPathInFolder() returned it.
 * @param[in] folder The folder the images must stay inside.
 *
 * @throws casacore::AipsError If a name is outside the folder, or is itself a saved image expression.
 */
void CheckExpressionNames(const std::string& expression, const std::string& directory, const std::string& folder);

/**
 * @brief Parses an image expression whose images must be inside the given folder.
 *
 * Checks the names as CheckExpressionNames() does first, then checks the names casacore reports having opened.
 *
 * @param[in] expression The image expression.
 * @param[in] directory The directory that names in the expression are relative to, as GetPathInFolder() returned it.
 * @param[in] folder The folder the images must stay inside.
 *
 * @return The node for the parsed expression.
 * @throws casacore::AipsError If the expression is refused or cannot be parsed.
 */
casacore::LatticeExprNode ParseImageExpression(const std::string& expression, const std::string& directory, const std::string& folder);

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
