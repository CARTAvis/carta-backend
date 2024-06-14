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

CARTA::FileType GuessImageType(const std::string& path_string, bool check_content);
CARTA::FileType GuessImageDirectoryType(const std::string& path_string, bool check_content);
CARTA::FileType GuessRegionType(const std::string& path_string, bool check_content);
CARTA::CatalogFileType GuessTableType(const std::string& path_string, bool check_content);

uint32_t GetMagicNumber(const std::string& filename);
bool IsCompressedFits(const std::string& filename);
bool IsGzMagicNumber(uint32_t magic_number);

// directory functions
int GetNumItems(const std::string& path);
fs::path SearchPath(std::string filename);

#endif // CARTA_SRC_UTIL_FILE_H_
