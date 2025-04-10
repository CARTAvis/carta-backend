/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "File.h"

#include <spdlog/fmt/fmt.h>
#include <fstream>
#include <regex>

#include "String.h"

/**
 * @details This function opens the specified file and reads the first 4 bytes as a `uint32_t` magic number.
 *
 * @note
 * - The function assumes the file's magic number is stored in the first 4 bytes.
 * - Reads the file in **binary mode** would be safer to avoid unwanted conversions.
 * - The byte order (endianness) of the magic number depends on the system architecture.
 *
 * @warning
 * - If the file does not exist or is not readable, the function returns `0` without error messages.
 */
uint32_t GetMagicNumber(const std::string& filename) {
    uint32_t magic_number = 0;

    std::ifstream input_file(filename);
    if (input_file && input_file.good()) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }

    return magic_number;
}

/**
 * @details This function first determines if the file is gzip-compressed by examining its
 * magic number. If the file is gzip-compressed, it then checks whether the
 * original filename (before compression) has a `.fits` extension.
 */
bool IsCompressedFits(const std::string& filename) {
    // Check if gzip file, then check .fits extension
    if (IsGzMagicNumber(GetMagicNumber(filename))) {
        fs::path gz_path(filename);
        std::string extension = gz_path.stem().extension().string();
        return HasSuffix(extension, ".fits");
    }

    return false;
}

/**
 * @details This function uses a regular expression (Regex) to determine if the provided filename
 * starts with "http://" or "https://", indicating that it is a remote file
 * accessible via HTTP.
 *
 * @note This function does not verify if the URL is accessible or valid beyond its prefix.
 */
bool IsRemoteHttpFile(const std::string& filename) {
    const std::regex is_http_url("^https?://");
    return std::regex_search(filename, is_http_url);
}

/**
 * @details This function checks whether the provided 32-bit magic number matches the
 * gzip file signature (`0x1f8b`). The magic number is formatted as a hexadecimal
 * string and examined to see if it ends with "8b1f".
 *
 * @note This function assumes little-endian byte order for checking the magic number.
 */
bool IsGzMagicNumber(uint32_t magic_number) {
    std::string hex_string = fmt::format("{:#x}", magic_number);
    return (hex_string.length() > 4) && (hex_string.substr(hex_string.length() - 4) == "8b1f");
}

/**
 * @details This function iterates over the contents of the specified directory and counts
 * the number of files and subdirectories present. If the directory does not exist
 * or cannot be accessed, the function returns `-1`.
 *
 * @note This function does not distinguish between files and subdirectories; it counts both.
 *
 * @warning If the path is invalid or inaccessible, an exception is caught internally,
 *          and `-1` is returned.
 */
int GetNumItems(const std::string& path) {
    try {
        int counter = 0;
        auto it = fs::directory_iterator(path);
        for (const auto& f : it) {
            counter++;
        }
        return counter;
    } catch (fs::filesystem_error) {
        return -1;
    }
}

// quick alternative to bp::search_path that allows us to remove
// boost:filesystem dependency
/**
 * @details This function retrieves the `PATH` environment variable, splits it into individual
 * directory paths, and searches for the specified file within those directories.
 * If the file is found, its full path is returned. If not found or if an error occurs,
 * an empty `fs::path` is returned.
 *
 * @note This function assumes that path entries in `PATH` are separated by colons (`:`),
 *       which is standard on UNIX-like systems.
 *
 * @warning If the `PATH` environment variable is not set, this function may behave unexpectedly.
 */
fs::path SearchPath(std::string filename) {
    std::string path(std::getenv("PATH"));
    std::vector<std::string> path_strings;
    SplitString(path, ':', path_strings);

    try {
        for (auto& p : path_strings) {
            fs::path base_path(p);
            base_path /= filename;
            if (fs::exists(base_path)) {
                return base_path;
            }
        }
    } catch (fs::filesystem_error) {
        return fs::path();
    }
    return fs::path();
}

/**
 * @details This function attempts to identify the type of an image file either by checking
 * its magic number (file signature) or examining its file extension. If `check_content`
 * is set to `true`, the function inspects the file's magic number to classify it as
 * FITS or HDF5. If `check_content` is `false`, it relies on common file extensions.
 *
 * @note When `check_content` is enabled, compressed FITS files (`.fits.gz`) are identified
 *       by their decompressed filename extension.
 *
 * @warning Checking the file content requires reading the file's magic number,
 *          which may introduce additional I/O overhead.
 */
CARTA::FileType GuessImageType(const std::string& path_string, bool check_content) {
    if (check_content) {
        // Guess file type by magic number
        auto magic_number = GetMagicNumber(path_string);
        if (magic_number == FITS_MAGIC_NUMBER) {
            return CARTA::FITS;
        } else if (magic_number == HDF5_MAGIC_NUMBER) {
            return CARTA::HDF5;
        } else if (IsGzMagicNumber(magic_number)) {
            fs::path gz_path(path_string);
            std::string extension = gz_path.stem().extension().string();
            return HasSuffix(extension, ".fits") ? CARTA::FITS : CARTA::UNKNOWN;
        }
    } else {
        // Guess file type by extension
        fs::path path(path_string);
        auto filename = path.filename().string();

        if (HasSuffix(filename, ".fits") || HasSuffix(filename, ".fz") || HasSuffix(filename, ".fits.gz")) {
            return CARTA::FITS;
        } else if (HasSuffix(filename, ".hdf5")) {
            return CARTA::HDF5;
        }
    }

    return CARTA::UNKNOWN;
}

/**
 * @details This function attempts to identify the type of a region file used in astronomical
 * imaging analysis by either checking its file content or examining its file extension.
 * If `check_content` is `true`, it reads the first line of the file to identify
 * known headers (e.g., `#CRTF` or `# Region file format: DS9`). Otherwise, it determines
 * the file type based on its extension.
 *
 * @note CRTF files typically start with `#CRTF`, while DS9 region files may include
 *       `# Region file format: DS9` as an optional header.
 *
 * @warning Checking file content requires reading the first line, which may introduce
 *          a slight I/O overhead.
 */
CARTA::FileType GuessRegionType(const std::string& path_string, bool check_content) {
    if (check_content) {
        // Check beginning of file for CRTF or REG header
        std::ifstream region_file(path_string);
        try {
            std::string first_line;
            if (!region_file.eof()) { // empty file
                getline(region_file, first_line);
            }
            region_file.close();

            if (first_line.find("#CRTF") == 0) {
                return CARTA::FileType::CRTF;
            } else if (first_line.find("# Region file format: DS9") == 0) { // optional header, but what else to do?
                return CARTA::FileType::DS9_REG;
            }
        } catch (std::ios_base::failure& f) {
            region_file.close();
        }
    } else {
        // Guess file type by extension
        fs::path path(path_string);
        auto filename = path.filename().string();

        if (HasSuffix(filename, ".crtf")) {
            return CARTA::CRTF;
        } else if (HasSuffix(filename, ".reg")) {
            return CARTA::DS9_REG;
        }
    }

    return CARTA::UNKNOWN;
}

/**
 * @details This function attempts to classify the type of a catalog table file (e.g., FITS table or VOTable)
 * either by checking its magic number (if `check_content` is `true`) or, if content checking is
 * disabled, by inspecting the file extension.
 *
 * @note If `check_content` is enabled, the function may attempt to read the file's magic number.
 *       Ensure the file is accessible to avoid potential I/O errors.
 *
 * @warning This function does not validate file integrity; it only determines type based on
 *          basic signature matching or filename extensions.
 */
CARTA::CatalogFileType GuessTableType(const std::string& path_string, bool check_content) {
    if (check_content) {
        uint32_t file_magic_number = GetMagicNumber(path_string);
        if (file_magic_number == XML_MAGIC_NUMBER) {
            return CARTA::CatalogFileType::VOTable;
        } else if (file_magic_number == FITS_MAGIC_NUMBER) {
            return CARTA::CatalogFileType::FITSTable;
        }
    } else {
        // Guess file type by extension
        fs::path path(path_string);
        auto filename = path.filename().string();

        if (HasSuffix(filename, ".fits") || HasSuffix(filename, ".fz") || HasSuffix(filename, ".fits.gz")) {
            return CARTA::CatalogFileType::FITSTable;
        } else if (HasSuffix(filename, ".xml") || HasSuffix(filename, ".vot") || HasSuffix(filename, ".votable")) {
            return CARTA::CatalogFileType::VOTable;
        }
    }

    return CARTA::CatalogFileType::Unknown;
}
