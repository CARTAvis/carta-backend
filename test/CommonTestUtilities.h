/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
#define CARTA_BACKEND__COMMON_TEST_UTILITIES_H_

#include <unordered_set>

#include <gtest/gtest.h>
#include <spdlog/fmt/fmt.h>

#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace carta;

fs::path TestRoot();

class ImageGenerator {
public:
    static std::string GeneratedFitsImagePath(const std::string& params);
    static std::string GeneratedHdf5ImagePath(const std::string& params);
};

class FileFinder {
public:
    static std::string DataPath(const std::string& filename);
    static std::string FitsImagePath(const std::string& filename);
    static std::string CasaImagePath(const std::string& filename);
    static std::string Hdf5ImagePath(const std::string& filename);
    static std::string FitsTablePath(const std::string& filename);
    static std::string XmlTablePath(const std::string& filename);
};

class CartaEnvironment : public ::testing::Environment {
public:
    virtual ~CartaEnvironment();
    void SetUp() override;
    void TearDown() override;
};

#endif // CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
