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
#include "Util/Casacore.h"
#include "Util/Message.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

fs::path TestRoot();
fs::path UserDirectory();

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

class DataReader {
public:
    virtual std::vector<float> ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) = 0;
    float ReadPointXY(hsize_t x, hsize_t y, hsize_t channel = 0, hsize_t stokes = 0);
    std::vector<float> ReadProfileX(hsize_t y, hsize_t channel = 0, hsize_t stokes = 0);
    std::vector<float> ReadProfileY(hsize_t x, hsize_t channel = 0, hsize_t stokes = 0);

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

bool OpenImage(std::shared_ptr<casacore::ImageInterface<float>>& image, const std::string& filename, uInt hdu_num = 0);
void GetImageData(std::vector<float>& data, std::shared_ptr<const casacore::ImageInterface<float>> image, int stokes,
    AxisRange z_range = AxisRange(ALL_Z), AxisRange x_range = AxisRange(ALL_X), AxisRange y_range = AxisRange(ALL_Y));
template <typename T>
std::vector<T> GetSpectralProfileValues(const CARTA::SpectralProfile& profile);
std::vector<float> GetSpatialProfileValues(const CARTA::SpatialProfile& profile);
void CompareVectors(const std::vector<float>& data1, const std::vector<float>& data2);
void CompareSpatialProfiles(
    const std::vector<CARTA::SpatialProfileData>& data_vec, const std::pair<std::vector<float>, std::vector<float>>& data_profiles);
void CompareHistograms(const carta::Histogram& cube_histogram1, const carta::Histogram& cube_histogram2);

#include "CommonTestUtilities.tcc"

#endif // CARTA_BACKEND__COMMON_TEST_UTILITIES_H_
