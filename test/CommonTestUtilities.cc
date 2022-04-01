/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <functional>
#include <stdexcept>

#include <gmock/gmock-matchers.h>

#include "CommonTestUtilities.h"
#include "Util/App.h"

using ::testing::NanSensitiveFloatNear;
using ::testing::Pointwise;

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

fs::path UserDirectory() {
    return fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX;
}

std::string ImageGenerator::GeneratedFitsImagePath(const std::string& params, const std::string& opts) {
    fs::path root = TestRoot();

    std::string filename = fmt::format("{:x}.fits", std::hash<std::string>{}(params + "/" + opts));
    fs::path fitspath = (root / "data" / "generated" / filename);
    std::string fitspath_str = fitspath.string();

    if (!fs::exists(fitspath)) {
        std::string generator_path = (root / "bin" / "make_image.py").string();
        std::string fitscmd = fmt::format("{} {} -o {} {}", generator_path, opts, fitspath_str, params);
        auto result = system(fitscmd.c_str());
    }

    return fitspath_str;
}

std::string ImageGenerator::GeneratedHdf5ImagePath(const std::string& params, const std::string& opts) {
    fs::path root = TestRoot();

    std::string fitspath_str = GeneratedFitsImagePath(params, opts);
    std::string hdf5path_str = fmt::format("{}.hdf5", fitspath_str);
    fs::path hdf5path = fs::path(hdf5path_str);

    if (!fs::exists(hdf5path)) {
        std::string hdf5cmd = fmt::format("fits2idia -o {} {}", hdf5path_str, fitspath_str);
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

float DataReader::ReadPointXY(hsize_t x, hsize_t y, hsize_t channel, hsize_t stokes) {
    return ReadRegion({x, y, channel, stokes}, {x + 1, y + 1, channel + 1, stokes + 1})[0];
}

std::vector<float> DataReader::ReadProfileX(hsize_t y, hsize_t channel, hsize_t stokes) {
    return ReadRegion({0, y, channel, stokes}, {_width, y + 1, channel + 1, stokes + 1});
}

std::vector<float> DataReader::ReadProfileY(hsize_t x, hsize_t channel, hsize_t stokes) {
    return ReadRegion({x, 0, channel, stokes}, {x + 1, _height, channel + 1, stokes + 1});
}

FitsDataReader::FitsDataReader(const std::string& imgpath) {
    int status(0);

    fits_open_file(&_imgfile, imgpath.c_str(), READONLY, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not open FITS file. Error status: {}", status));
    }

    int bitpix;
    fits_get_img_type(_imgfile, &bitpix, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image type. Error status: {}", status));
    }

    if (bitpix != -32) {
        throw std::runtime_error("Currently only supports FP32 files");
    }

    fits_get_img_dim(_imgfile, &_N, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image dimensions. Error status: {}", status));
    }

    if (_N < 2 || _N > 4) {
        throw std::runtime_error("Currently only supports 2D, 3D and 4D cubes");
    }

    std::vector<long> dims(_N);

    fits_get_img_size(_imgfile, _N, dims.data(), &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image size. Error status: {}", status));
    }

    for (auto& d : dims) {
        _dims.push_back(d);
    }
    _stokes = _N == 4 ? _dims[3] : 1;
    _depth = _N >= 3 ? _dims[2] : 1;
    _height = _dims[1];
    _width = _dims[0];
}

FitsDataReader::~FitsDataReader() {
    int status(0);

    if (_imgfile) {
        fits_close_file(_imgfile, &status);
    }
}

std::vector<float> FitsDataReader::ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) {
    int status(0);
    std::vector<float> result;

    long fpixel[_N];
    long lpixel[_N];
    long inc[_N];
    long result_size = 1;

    for (int d = 0; d < _N; d++) {
        // Truncate or extend the first and last pixel array to the image dimensions
        // ...and convert from 0-indexing to 1-indexing
        // ...and convert end vector from exclusive to inclusive
        fpixel[d] = d < start.size() ? start[d] + 1 : 1;
        lpixel[d] = d < end.size() ? end[d] : 1;
        // Set the increment to 1
        inc[d] = 1;

        // Calculate the expected result size
        result_size *= lpixel[d] - fpixel[d] + 1;
    }

    result.resize(result_size);

    fits_read_subset(_imgfile, TFLOAT, fpixel, lpixel, inc, nullptr, result.data(), nullptr, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image data. Error status: {}", status));
    }

    return result;
}

Hdf5DataReader::Hdf5DataReader(const std::string& imgpath) {
    _imgfile = H5::H5File(imgpath, H5F_ACC_RDONLY);
    _group = _imgfile.openGroup("0");
    _dataset = _group.openDataSet("DATA");

    auto data_space = _dataset.getSpace();
    _N = data_space.getSimpleExtentNdims();
    _dims.resize(_N);
    data_space.getSimpleExtentDims(_dims.data(), nullptr);

    std::reverse(_dims.begin(), _dims.end());
    _stokes = _N == 4 ? _dims[3] : 1;
    _depth = _N >= 3 ? _dims[2] : 1;
    _height = _dims[1];
    _width = _dims[0];
}

std::vector<float> Hdf5DataReader::ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) {
    std::vector<float> result;
    std::vector<hsize_t> h5_start;
    std::vector<hsize_t> h5_count;
    hsize_t result_size = 1;

    for (int d = 0; d < _N; d++) {
        // Calculate the expected result size
        h5_start.insert(h5_start.begin(), d < start.size() ? start[d] : 0);
        h5_count.insert(h5_count.begin(), d < start.size() ? end[d] - start[d] : 1);
        result_size *= end[d] - start[d];
    }

    result.resize(result_size);
    H5::DataSpace mem_space(1, &result_size);

    auto file_space = _dataset.getSpace();
    file_space.selectHyperslab(H5S_SELECT_SET, h5_count.data(), h5_start.data());
    _dataset.read(result.data(), H5::PredType::NATIVE_FLOAT, mem_space, file_space);

    return result;
}

hid_t Hdf5DataReader::GroupId() {
    return _group.getId();
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

void CmpVectors(const std::vector<float>& data1, const std::vector<float>& data2, float abs_err) {
    EXPECT_THAT(data1, Pointwise(NanSensitiveFloatNear(1e-5), data2));
    
//     EXPECT_EQ(data1.size(), data2.size());
//     if (data1.size() == data2.size()) {
//         for (int i = 0; i < data1.size(); ++i) {
//             CmpValues(data1[i], data2[i], abs_err);
//         }
//     }
}

void CmpValues(float data1, float data2, float abs_err) {
    if (!std::isnan(data1) || !std::isnan(data2)) {
        if (abs_err > 0) {
            EXPECT_NEAR(data1, data2, abs_err);
        } else {
            EXPECT_FLOAT_EQ(data1, data2);
        }
    }
}
