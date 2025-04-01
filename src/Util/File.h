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
 * @param[in] path_string The path to the image file to be identified.
 * @param[in] check_content If `true`, the function checks the file's magic number;
 *                          otherwise, it checks the file extension.
 *
 * @return The detected file type as a `CARTA::FileType` enumeration:
 *         - `CARTA::FITS` for FITS files.
 *         - `CARTA::HDF5` for HDF5 files.
 *         - `CARTA::UNKNOWN` if the file type cannot be determined.
 */
CARTA::FileType GuessImageType(const std::string& path_string, bool check_content);

/**
 * @brief Determines the region file type based on its content or extension.
 *
 * @param[in] path_string The path to the region file to be identified.
 * @param[in] check_content If `true`, the function checks the file's header;
 *                          otherwise, it relies on the file extension.
 *
 * @return The detected file type as a `CARTA::FileType` enumeration:
 *         - `CARTA::CRTF` for CASA Region Text Format (CRTF) files.
 *         - `CARTA::DS9_REG` for DS9 region files.
 *         - `CARTA::UNKNOWN` if the file type cannot be determined.
 */
CARTA::FileType GuessRegionType(const std::string& path_string, bool check_content);

/**
 * @brief Determines the catalog table file type based on its content or file extension.
 *
 * @param path_string The file path as a string.
 * @param check_content If `true`, the function checks the file's magic number for type detection.
 *                      If `false`, it relies on the file extension.
 * @return CARTA::CatalogFileType The detected file type, which can be:
 *         - `CARTA::CatalogFileType::FITSTable` for FITS table files.
 *         - `CARTA::CatalogFileType::VOTable` for XML-based VOTable files.
 *         - `CARTA::CatalogFileType::Unknown` if the type cannot be determined.
 */
CARTA::CatalogFileType GuessTableType(const std::string& path_string, bool check_content);

/**
 * @brief Reads the magic number from the beginning of a file.
 *
 * @param filename The path to the file.
 *
 * @return The magic number as a `uint32_t`. If the file cannot be opened or read, returns `0`.
 */
uint32_t GetMagicNumber(const std::string& filename);

/**
 * @brief Checks whether a given file is a compressed FITS file.
 *
 * @param[in] filename The path to the file being checked.
 *
 * @return `true` if the file is a gzip-compressed FITS file, otherwise `false`.
 */
bool IsCompressedFits(const std::string& filename);

/**
 * @brief Determines if a given magic number corresponds to a gzip-compressed file.
 *
 * @param[in] magic_number The 32-bit magic number extracted from the file.
 *
 * @return `true` if the magic number matches the gzip signature, otherwise `false`.
 */
bool IsGzMagicNumber(uint32_t magic_number);

/**
 * @brief Checks whether a given filename represents a remote HTTP or HTTPS URL.
 *
 * @param[in] filename The string to be checked.
 *
 * @return `true` if the filename is an HTTP or HTTPS URL, otherwise `false`.
 */
bool IsRemoteHttpFile(const std::string& filename);

/**
 * @brief Counts the number of items in a given directory.
 *
 * @param[in] path The path to the directory whose items are to be counted.
 *
 * @return The number of items in the directory, or `-1` if the directory cannot be accessed.
 */
int GetNumItems(const std::string& path);

/**
 * @brief Searches for a file in the system's `PATH` environment variable.
 *
 * @param[in] filename The name of the file to search for.
 *
 * @return The full path to the located file, or an empty `fs::path` if not found.
 */
fs::path SearchPath(std::string filename);

#endif // CARTA_SRC_UTIL_FILE_H_
