/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
#define CARTA_BACKEND__COMMON_TEST_UTILITIES_H_

#include <unordered_set>

#include <H5Cpp.h>
#include <fitsio.h>
#include <gtest/gtest.h>
#include <spdlog/fmt/fmt.h>

#include "App.h"

#include <filesystem>
namespace fs = std::filesystem;

fs::path TestRoot();
fs::path UserDirectory();

class ImageGenerator {
public:
    static std::string GeneratedFitsImagePath(const std::string& params, const std::string& opts = "-s 0");
    static std::string GeneratedHdf5ImagePath(const std::string& params, const std::string& opts = "-s 0");
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

class DataReader {
public:
    virtual std::vector<float> ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) = 0;
    float ReadPointXY(hsize_t x, hsize_t y, hsize_t channel = 0, hsize_t stokes = 0);
    std::vector<float> ReadProfileX(hsize_t y, hsize_t channel = 0, hsize_t stokes = 0);
    std::vector<float> ReadProfileY(hsize_t x, hsize_t channel = 0, hsize_t stokes = 0);
    std::vector<float> ReadXY(hsize_t channel = 0, hsize_t stokes = 0);
    hsize_t Width();
    hsize_t Height();

protected:
    int _N;
    std::vector<hsize_t> _dims;
    hsize_t _stokes, _depth, _height, _width;
};

class FitsDataReader : public DataReader {
public:
    FitsDataReader(const std::string& imgpath);
    ~FitsDataReader();
    std::vector<float> ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) override;

private:
    fitsfile* _imgfile;
};

class Hdf5DataReader : public DataReader {
public:
    Hdf5DataReader(const std::string& imgpath);
    ~Hdf5DataReader() = default;
    std::vector<float> ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) override;
    hid_t GroupId();

private:
    H5::H5File _imgfile;
    H5::Group _group;
    H5::DataSet _dataset;
};

class CartaEnvironment : public ::testing::Environment {
public:
    virtual ~CartaEnvironment();
    void SetUp() override;
    void TearDown() override;
};

void CmpValues(float data1, float data2, float abs_err = 0);
void CmpVectors(const std::vector<float>& data1, const std::vector<float>& data2, float abs_err = 0);

#endif // CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
