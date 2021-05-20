/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
#define CARTA_BACKEND__COMMON_TEST_UTILITIES_H_

#include <unordered_set>

#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <spdlog/fmt/fmt.h>

using namespace carta;

class ImageGenerator {
public:
    static std::string GenerateFitsImage(const std::string& params);
    static std::string GenerateHdf5Image(const std::string& params);
    static void DeleteImages();

private:
    static std::unordered_set<std::string> generated_images;
};

#endif // CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
