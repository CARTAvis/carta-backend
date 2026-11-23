/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Casacore.h"

#include <algorithm>
#include <regex>

#include <casacore/casa/OS/File.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/images/Images/ImageExprParse.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"
#include "Util/FileSystem.h"

const std::regex GILDAS_REGEX(" *[a-zA-Z]+[ .]+\\(T[a-zA-Z_]+[*.]*\\) *");

/**
 * @details This function constructs an absolute file path using a specified root directory,
 * a relative subdirectory, and a file name. It checks whether the resulting file path
 * exists and is readable. If any issue is encountered, an error message is set.
 */
casacore::String GetResolvedFilename(
    const std::string& root_dir, const std::string& directory, const std::string& file, std::string& message, std::string* checked_dir) {
    // Given directory (relative to root directory) and file, return resolved file path.
    // Check if file path exists and is readable.
    casacore::String resolved_filename;
    casacore::Path dir_path(root_dir);
    dir_path.append(directory);
    casacore::Path file_path(dir_path);
    file_path.append(file);

    auto dir_name = GetPathInFolder(dir_path.originalName(), root_dir);
    auto file_name = GetPathInFolder(file_path.originalName(), root_dir);
    if (dir_name.empty() || file_name.empty()) {
        message = "Forbidden path.";
        return resolved_filename;
    }

    casacore::Path path(dir_name);
    casacore::File cc_file(path);

    // Check directory
    if (!cc_file.exists()) {
        message = fmt::format("Directory {} does not exist.", path.expandedName());
    } else if (!cc_file.isReadable()) {
        message = fmt::format("Directory {} is not readable.", path.expandedName());
    } else {
        // Check file
        path = casacore::Path(file_name);
        cc_file = casacore::File(path);
        if (!cc_file.exists()) {
            message = fmt::format("File {} does not exist.", path.expandedName());
        } else if (!cc_file.isReadable()) {
            message = fmt::format("File {} is not readable.", path.expandedName());
        } else {
            try {
                resolved_filename = path.resolvedName();
            } catch (const casacore::AipsError& err) {
                // resolvedName() calls absoluteName(), which returns an empty path due to parsing bug to remove dots (., ..) from path,
                // resulting in AipsError. Workaround just sets expanded name used for exists() and isReadable().
                if (path.absoluteName().empty()) {
                    resolved_filename = path.expandedName();
                } else {
                    message = fmt::format("Error resolving file {} : {}", path.expandedName(), err.getMesg());
                }
            }
        }
    }

    if (checked_dir && !resolved_filename.empty()) {
        *checked_dir = dir_name;
    }
    return resolved_filename;
}

std::string GetPathInFolder(const std::string& path, const std::string& folder, bool expand) {
    auto normalized = fs::path(expand ? casacore::Path(path).expandedName().c_str() : path.c_str()).lexically_normal();
    if (!normalized.has_filename() && normalized.has_relative_path()) {
        normalized = normalized.parent_path(); // a/b/.. is left as a/
    }
    auto relative = normalized.lexically_relative(fs::path(folder).lexically_normal());
    // Also refuse any .. left behind, in case lexically_normal() misses one (older libstdc++ has had bugs here)
    if (relative.empty() || *relative.begin() == ".." || std::find(normalized.begin(), normalized.end(), "..") != normalized.end()) {
        return "";
    }
    return normalized.string();
}

// Throws unless casacore can be given this image name from an expression. The folder and the second directory have had
// their symlinks followed.
static void CheckImageName(
    const std::string& name, const fs::path& directory, const fs::path& resolved_directory, const std::string& top_level_folder) {
    if (name.empty()) {
        return;
    }
    std::error_code error_code;
    auto in_folder = [&](const fs::path& path) { return !GetPathInFolder(path.string(), top_level_folder, false).empty(); };

    // casacore opens an absolute name as it is, after expanding ~ and $VAR, and any other name relative to the directory
    fs::path expanded(casacore::Path(name).expandedName().c_str());
    fs::path upward; // the name as far as its last ..
    size_t num_parts(0), last_up(0);
    for (const auto& part : expanded) {
        ++num_parts;
        if (part == "..") {
            last_up = num_parts;
        }
    }
    for (auto part = expanded.begin(); last_up > 0; ++part, --last_up) {
        upward /= *part;
    }

    bool allowed;
    if (upward.empty()) {
        // A relative name with no .. stays inside the directory, which is already in the folder (symlinks in it may point
        // anywhere). An absolute name has to be in the folder as written, or once its symlinks are followed.
        allowed = expanded.is_relative() || in_folder(expanded) || in_folder(fs::weakly_canonical(expanded, error_code));
    } else {
        // casacore takes a .. out of the text in some places, and in others leaves the OS to follow symlinks first. Both
        // have to end up in the folder, or where the path leads when the client's own text is read the first way.
        auto text = (directory / upward).lexically_normal();
        auto linked = in_folder(text) ? fs::weakly_canonical(text, error_code) : fs::path();
        auto given = resolved_directory / upward;
        allowed = !resolved_directory.empty();
        for (const auto& end : {fs::weakly_canonical(given, error_code), fs::weakly_canonical(given.lexically_normal(), error_code)}) {
            allowed = allowed && !end.empty() && (in_folder(end) || end / "" == linked / "");
        }
    }

    if (!allowed) {
        throw casacore::AipsError("Image expression cannot use " + name + ": names must be inside the top-level folder.");
    }
    // casacore opens the names in a saved expression relative to the working directory, and nothing checks them
    if (fs::is_regular_file(directory / expanded / "imageexpr.json", error_code)) {
        throw casacore::AipsError("Image expression cannot use the saved image expression " + name + ".");
    }
}

// CheckExpressionNames() with the folder and the second directory already resolved. This copies casacore's lexer rules; a
// check in casacore's ImageExprParse::addDir, which sees each name before it opens it, could replace it upstream.
static void CheckNames(
    const std::string& expression, const std::string& directory, const fs::path& resolved_directory, const std::string& top_level_folder) {
    // casacore removes backslash escapes in its lexer, and again when it takes the image name from before any :mask or ::region
    auto unescape = [](const std::string& text, bool stop_at_colon) {
        std::string out;
        for (size_t i = 0; i < text.size() && !(stop_at_colon && text[i] == ':'); ++i) {
            i += (text[i] == '\\');
            if (i < text.size()) {
                out += text[i];
            }
        }
        return out;
    };
    auto check_name = [&](const std::string& text) {
        // A mask with no image name before it makes casacore open its working directory
        if (text.size() > 1 && text[0] == ':' && text[1] != ':') {
            throw casacore::AipsError("Image expression cannot use " + text + ": there is no image name before the colon.");
        }
        CheckImageName(unescape(text, true), directory, resolved_directory, top_level_folder);
    };

    auto is_digit_or_dot = [&](size_t pos) {
        return pos < expression.size() && ((expression[pos] >= '0' && expression[pos] <= '9') || expression[pos] == '.');
    };
    // An unquoted name starts with a letter, one of _ ~ $ or any escaped character, and goes on with those, digits and dots
    auto is_name_start = [&](size_t pos) {
        char c = pos < expression.size() ? expression[pos] : ' ';
        if (c == '\\') {
            return pos + 1 < expression.size() && expression[pos + 1] != '\n';
        }
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '~' || c == '$';
    };
    auto name_end = [&](size_t pos) {
        while (is_name_start(pos) || is_digit_or_dot(pos)) {
            pos += (expression[pos] == '\\') ? 2 : 1;
        }
        return pos;
    };

    // Find the names as casacore's lexer does. Function and constant names are checked too: with no / or .. they pass,
    // unless the directory holds a saved image expression of the same name.
    for (size_t pos = 0; pos < expression.size();) {
        char c = expression[pos];
        if (c == '"' || c == '\'') {
            // casacore joins quoted strings that touch into one name, so "..""/x.fits" has to be checked as ../x.fits
            std::string text;
            bool terminated(true);
            while (terminated && pos < expression.size() && (expression[pos] == '"' || expression[pos] == '\'')) {
                auto close = expression.find_first_of(std::string{expression[pos], '\n'}, pos + 1);
                terminated = (close != std::string::npos && expression[close] != '\n');
                if (terminated) {
                    text += expression.substr(pos + 1, close - pos - 1);
                    pos = close + 1;
                }
            }
            check_name(text);
            if (!terminated) {
                return; // casacore stops with an error here, so it reads nothing more
            }
        } else if (is_name_start(pos) || c == ':') {
            // An unquoted name can go on past one colon or two, if another name follows them directly
            auto end = name_end(pos);
            if (expression.compare(end, 1, ":") == 0) {
                auto next = end + (expression.compare(end, 2, "::") == 0 ? 2 : 1);
                end = is_name_start(next) ? name_end(next) : std::max(end, pos + 1);
            }
            check_name(unescape(expression.substr(pos, end - pos), false));
            pos = end;
        } else if (is_digit_or_dot(pos)) {
            // casacore reads this as a number, and gives an error if a name follows it directly. That name is not checked,
            // so refuse a number with anything in it that could make a path, in case casacore ever does open one.
            auto end = name_end(pos);
            auto text = unescape(expression.substr(pos, end - pos), false);
            if (text.find_first_of("/~$\\") != std::string::npos || text.find("..") != std::string::npos) {
                throw casacore::AipsError("Image expression cannot use " + text + ": names must be inside the top-level folder.");
            }
            pos = end;
        } else {
            ++pos;
        }
    }
}

void CheckExpressionNames(const std::string& expression, const std::string& directory, const std::string& folder) {
    std::error_code error_code;
    auto top_level_folder = fs::weakly_canonical(fs::absolute(folder, error_code), error_code).string();
    if (top_level_folder != "/") {
        CheckNames(expression, directory, fs::weakly_canonical(fs::absolute(directory, error_code), error_code), top_level_folder);
    }
}

casacore::LatticeExprNode ParseImageExpression(const std::string& expression, const std::string& directory, const std::string& folder) {
    std::error_code error_code;
    auto top_level_folder = fs::weakly_canonical(fs::absolute(folder, error_code), error_code).string();
    // casacore gets the directory with its symlinks followed, so that a .. in a name starts from where the directory really is
    auto resolved_directory = fs::weakly_canonical(fs::absolute(directory, error_code), error_code);
    bool restricted = top_level_folder != "/";
    if (restricted) {
        CheckNames(expression, directory, resolved_directory, top_level_folder);
    }
    if (resolved_directory.empty()) {
        throw casacore::AipsError("Image expression directory " + directory + " cannot be resolved.");
    }
    auto node = casacore::ImageExprParse::command(expression, resolved_directory.string());

    // casacore keeps the names of the images it opened, so check those too in case it read the expression differently
    if (restricted) {
        for (const auto& name : casacore::ImageExprParse::getImageNames()) {
            CheckImageName(name, directory, resolved_directory, top_level_folder);
        }
    }
    return node;
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
