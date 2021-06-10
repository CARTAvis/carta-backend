/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <functional>

#include "CommonTestUtilities.h"

using namespace carta;

fs::path TestRoot() {
    std::string path_string;
    fs::path root;
    if (FindExecutablePath(path_string)) {
        root = fs::path(path_string).lexically_normal().parent_path();
    } else {
        root = fs::current_path();
    }
    return root;
}

std::string ImageGenerator::GeneratedFitsImagePath(const std::string& params) {
    fs::path root = TestRoot();

    std::string filename = fmt::format("{:x}.fits", std::hash<std::string>{}(params));
    fs::path fitspath = (root / "data" / "generated" / filename);
    std::string fitspath_str = fitspath.string();

    if (!fs::exists(fitspath)) {
        std::string generator_path = (root / "bin" / "make_image.py").string();
        std::string fitscmd = fmt::format("{} -s 0 -o {} {}", generator_path, fitspath_str, params);
        auto result = system(fitscmd.c_str());
    }

    return fitspath_str;
}

std::string ImageGenerator::GeneratedHdf5ImagePath(const std::string& params) {
    fs::path root = TestRoot();

    std::string fitspath_str = GeneratedFitsImagePath(params);
    std::string hdf5path_str = fmt::format("{}.hdf5", fitspath_str);
    fs::path hdf5path = fs::path(hdf5path_str);

    if (!fs::exists(hdf5path)) {
        std::string converter_path = (root / "bin" / "fits2idia").string();
        std::string hdf5cmd = fmt::format("{} -o {} {}", converter_path, hdf5path_str, fitspath_str);
        auto result = system(hdf5cmd.c_str());
    }

    return hdf5path_str;
}

std::string FileFinder::DataPath(const std::string& filename) {
    return (TestRoot() / "data" / filename).string();
}

std::string FileFinder::FitsImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "fits" / filename).string();
}

std::string FileFinder::CasaImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "casa" / filename).string();
}

std::string FileFinder::Hdf5ImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "hdf5" / filename).string();
}

std::string FileFinder::FitsTablePath(const std::string& filename) {
    return (TestRoot() / "data" / "tables" / "fits" / filename).string();
}

std::string FileFinder::XmlTablePath(const std::string& filename) {
    return (TestRoot() / "data" / "tables" / "xml" / filename).string();
}

CartaEnvironment::~CartaEnvironment() {}

void CartaEnvironment::SetUp() {
    // create directory for generated images
    fs::create_directory(TestRoot() / "data" / "generated");
}

void CartaEnvironment::TearDown() {
    // delete directory of generated images
    fs::remove_all(TestRoot() / "data" / "generated");
}
