/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "File.h"

#include <fstream>

#include "String.h"

uint32_t GetMagicNumber(const std::string& filename) {
    uint32_t magic_number = 0;

    std::ifstream input_file(filename);
    if (input_file && input_file.good()) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }

    return magic_number;
}

bool IsCompressedFits(const std::string& filename) {
    // Check if gzip file, then check .fits extension
    auto magic_number = GetMagicNumber(filename);
    if (magic_number == GZ_MAGIC_NUMBER) {
        fs::path gz_path(filename);
        std::string extension = gz_path.stem().extension().string();
        return HasSuffix(extension, ".fits");
    }

    return false;
}

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

CARTA::FileType GuessImageType(const std::string& path_string, bool check_content) {
    if (check_content) {
        // Guess file type by magic number
        auto magic_number = GetMagicNumber(path_string);
        switch (magic_number) {
            case FITS_MAGIC_NUMBER:
                return CARTA::FITS;
            case HDF5_MAGIC_NUMBER:
                return CARTA::HDF5;
            case GZ_MAGIC_NUMBER:
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
