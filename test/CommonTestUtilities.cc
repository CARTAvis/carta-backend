/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <functional>

#include "CommonTestUtilities.h"

using namespace carta;

std::string ImageGenerator::GenerateFitsImage(const std::string& params) {
    std::string path_string;
    fs::path root;
    if (FindExecutablePath(path_string)) {
        root = fs::path(path_string).parent_path();
    } else {
        root = fs::current_path();
    }

    std::string filename = fmt::format("{:x}.fits", std::hash<std::string>{}(params));
    std::string fitspath = (root / "data/generated" / filename).string();

    std::string fitscmd = fmt::format("./bin/make_image.py -s 0 -o {} {}", fitspath, params);
    auto result = system(fitscmd.c_str());

    generated_images.insert(fitspath);
    return fitspath;
}

std::string ImageGenerator::GenerateHdf5Image(const std::string& params) {
    std::string fitspath = ImageGenerator::GenerateFitsImage(params);
    std::string hdf5path = fmt::format("{}.hdf5", fitspath);

    std::string hdf5cmd = fmt::format("./bin/fits2idia -o {} {}", hdf5path, fitspath);
    auto result = system(hdf5cmd.c_str());

    generated_images.insert(hdf5path);
    return hdf5path;
}

// TODO simplify all of this; create and remove the whole directory from here
void ImageGenerator::DeleteImages() {
    for (auto imgpath : generated_images) {
        std::string rmcmd = fmt::format("rm {}", imgpath);
        auto result = system(rmcmd.c_str());
    }
    generated_images.clear();
}

std::unordered_set<std::string> ImageGenerator::generated_images;
