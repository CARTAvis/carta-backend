/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "File.h"

#include <fstream>

#include "String.h"

uint32_t GetMagicNumber(const std::string& filename) {
    uint32_t magic_number = 0;

    std::ifstream input_file(filename);
    if (input_file) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }

    return magic_number;
}

bool IsCompressedFits(const std::string& filename) {
    // Check if gzip file, then check .fits extension
    bool is_fits(false);
    auto magic_number = GetMagicNumber(filename);
    if (magic_number == GZ_MAGIC_NUMBER) {
        fs::path gz_path(filename);
        is_fits = (gz_path.stem().extension().string() == ".fits");
    }

    return is_fits;
}

int GetNumItems(const std::string& path) {
    try {
        int counter = 0;
        auto it = fs::directory_iterator(path);
        for (const auto f : it) {
            counter++;
        }
        return counter;
    } catch (std::exception) {
        return -1;
    }
}

// quick alternative to bp::search_path that allows us to remove
// boost:filesystem dependency
fs::path SearchPath(std::string filename) {
    std::string path(std::getenv("PATH"));
    std::vector<std::string> path_strings;
    SplitString(path, ':', path_strings);
    for (auto& p : path_strings) {
        fs::path base_path(p);
        base_path /= filename;
        if (fs::exists(base_path)) {
            return base_path;
        }
    }
    return fs::path();
}
