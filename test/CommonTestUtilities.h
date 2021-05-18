/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
#define CARTA_BACKEND__COMMON_TEST_UTILITIES_H_

#include <functional>

#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <spdlog/fmt/fmt.h>

using namespace std;
using namespace carta;

class ImageGenerator {
public:
    static string GenerateFitsImage(const string& params);
    static string GenerateHdf5Image(const string& params);
    static void DeleteImages();

private:
    static unordered_set<string> generated_images;
};

#endif // CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
