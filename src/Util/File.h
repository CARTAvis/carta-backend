/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_FILE_H_
#define CARTA_SRC_UTIL_FILE_H_

#include <carta-protobuf/enums.pb.h>

#include "FileSystem.h"

// Valid for little-endian only
#define FITS_MAGIC_NUMBER 0x504D4953
#define HDF5_MAGIC_NUMBER 0x46444889
#define XML_MAGIC_NUMBER 0x6D783F3C

// file list
#define FILE_LIST_FIRST_PROGRESS_AFTER_SECS 5
#define FILE_LIST_PROGRESS_INTERVAL_SECS 2

// file ids
#define ALL_FILES -1
#define TEMP_FILE_ID -100

/**
 * @brief Determines the image file type based on its content or extension.
 *
 * This function attempts to identify the type of an image file by either checking
 * its magic number (file signature) or examining its file extension. If `check_content`
 * is set to `true`, the function inspects the file's magic number to classify it as
 * FITS or HDF5. If `check_content` is `false`, it relies on common file extensions.
 *
 * @param[in] path_string The path to the image file to be identified.
 * @param[in] check_content If `true`, the function checks the file's magic number;
 *                          otherwise, it checks the file extension.
 *
 * @return The detected file type as a `CARTA::FileType` enumeration:
 *         - `CARTA::FITS` for FITS files.
 *         - `CARTA::HDF5` for HDF5 files.
 *         - `CARTA::UNKNOWN` if the file type cannot be determined.
 *
 * @note When `check_content` is enabled, compressed FITS files (`.fits.gz`) are identified
 *       by checking their decompressed filename extension.
 *
 * @warning Checking the file content requires reading the file's magic number,
 *          which may introduce additional I/O overhead.
 */
CARTA::FileType GuessImageType(const std::string& path_string, bool check_content);

/**
 * @brief Determines the region file type based on its content or extension.
 *
 * This function attempts to identify the type of a region file used in astronomical
 * imaging analysis by either checking its file content or examining its file extension.
 * If `check_content` is `true`, it reads the first line of the file to identify
 * known headers (e.g., `#CRTF` or `# Region file format: DS9`). Otherwise, it determines
 * the file type based on its extension.
 *
 * @param[in] path_string The path to the region file to be identified.
 * @param[in] check_content If `true`, the function checks the file's header;
 *                          otherwise, it relies on the file extension.
 *
 * @return The detected file type as a `CARTA::FileType` enumeration:
 *         - `CARTA::CRTF` for CASA Region Text Format (CRTF) files.
 *         - `CARTA::DS9_REG` for DS9 region files.
 *         - `CARTA::UNKNOWN` if the file type cannot be determined.
 *
 * @note CRTF files typically start with `#CRTF`, while DS9 region files may include
 *       `# Region file format: DS9` as an optional header.
 *
 * @warning Checking file content requires reading the first line, which may introduce
 *          a slight I/O overhead.
 */
CARTA::FileType GuessRegionType(const std::string& path_string, bool check_content);

/**
 * @brief Determines the catalog table file type based on its content or file extension.
 *
 * This function attempts to classify the type of a catalog table file (e.g., FITS table or VOTable)
 * by first checking its magic number (if `check_content` is `true`) or, if content checking is
 * disabled, by inspecting the file extension.
 *
 * @param path_string The file path as a string.
 * @param check_content If `true`, the function checks the file's magic number for type detection.
 *                      If `false`, it relies on the file extension.
 * @return CARTA::CatalogFileType The detected file type, which can be:
 *         - `CARTA::CatalogFileType::FITSTable` for FITS table files.
 *         - `CARTA::CatalogFileType::VOTable` for XML-based VOTable files.
 *         - `CARTA::CatalogFileType::Unknown` if the type cannot be determined.
 *
 * @note If `check_content` is enabled, the function may attempt to read the file's magic number.
 *       Ensure the file is accessible to avoid potential I/O errors.
 *
 * @warning This function does not validate file integrity, it only determines type based on
 *          basic signature matching or filename extensions.
 */
CARTA::CatalogFileType GuessTableType(const std::string& path_string, bool check_content);

/**
 * @brief Reads the magic number from the beginning of a file.
 *
 * This function opens the specified file and reads the first 4 bytes as a `uint32_t` magic number.
 *
 * @param filename The path to the file.
 *
 * @return The magic number as a `uint32_t`. If the file cannot be opened or read, returns `0`.
 *
 * @note
 * - The function assumes the file's magic number is stored in the first 4 bytes.
 * - Reads the file in **binary mode** would be safer to avoid unwanted conversions.
 * - The byte order (endianness) of the magic number depends on the system architecture.
 *
 * @warning
 * - If the file does not exist or is not readable, the function returns `0` without error messages.
 */
uint32_t GetMagicNumber(const std::string& filename);

/**
 * @brief Checks whether a given file is a compressed FITS file.
 *
 * This function first determines if the file is gzip-compressed by examining its
 * magic number. If the file is gzip-compressed, it then checks whether the
 * original filename (before compression) has a `.fits` extension.
 *
 * @param[in] filename The path to the file being checked.
 *
 * @return `true` if the file is a gzip-compressed FITS file, otherwise `false`.
 */
bool IsCompressedFits(const std::string& filename);

/**
 * @brief Determines if a given magic number corresponds to a gzip-compressed file.
 *
 * This function checks whether the provided 32-bit magic number matches the
 * gzip file signature (`0x1f8b`). The magic number is formatted as a hexadecimal
 * string and examined to see if it ends with "8b1f".
 *
 * @param[in] magic_number The 32-bit magic number extracted from the file.
 *
 * @return `true` if the magic number matches the gzip signature, otherwise `false`.
 *
 * @note This function assumes little-endian byte order for checking the magic number.
 */
bool IsGzMagicNumber(uint32_t magic_number);

/**
 * @brief Checks whether a given filename represents a remote HTTP or HTTPS URL.
 *
 * This function uses a regular expression (Regex) to determine if the provided filename
 * starts with "http://" or "https://", indicating that it is a remote file
 * accessible via HTTP.
 *
 * @param[in] filename The string to be checked.
 *
 * @return `true` if the filename is an HTTP or HTTPS URL, otherwise `false`.
 *
 * @note This function does not verify if the URL is accessible or valid beyond its prefix.
 */
bool IsRemoteHttpFile(const std::string& filename);

/**
 * @brief Counts the number of items in a given directory.
 *
 * This function iterates over the contents of the specified directory and counts
 * the number of files and subdirectories present. If the directory does not exist
 * or cannot be accessed, the function returns `-1`.
 *
 * @param[in] path The path to the directory whose items are to be counted.
 *
 * @return The number of items in the directory, or `-1` if the directory cannot be accessed.
 *
 * @note This function does not distinguish between files and subdirectories; it counts both.
 *
 * @warning If the path is invalid or inaccessible, an exception is caught internally,
 *          and `-1` is returned.
 */
int GetNumItems(const std::string& path);

/**
 * @brief Searches for a file in the system's `PATH` environment variable.
 *
 * This function retrieves the `PATH` environment variable, splits it into individual
 * directory paths, and searches for the specified file within those directories.
 * If the file is found, its full path is returned. If not found or if an error occurs,
 * an empty `fs::path` is returned.
 *
 * @param[in] filename The name of the file to search for.
 *
 * @return The full path to the located file, or an empty `fs::path` if not found.
 *
 * @note This function assumes that path entries in `PATH` are separated by colons (`:`),
 *       which is standard on UNIX-like systems. It may need modification for Windows.
 *
 * @warning If the `PATH` environment variable is not set, this function may behave unexpectedly.
 */
fs::path SearchPath(std::string filename);

#endif // CARTA_SRC_UTIL_FILE_H_
