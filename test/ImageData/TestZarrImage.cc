/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <casacore/casa/Quanta/Unit.h>
#include <casacore/measures/Measures/Stokes.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "CommonTestUtilities.h"
#include "FileList/FileInfoLoader.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrImage.h"
#include "ImageData/ZarrLoader.h"
#include "ImageData/ZarrStore.h"

namespace {

void WriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output.is_open());
    output << json;
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open " + path.string());
    }
    return nlohmann::json::parse(input);
}

void WriteFixedLengthUtf32Strings(const std::filesystem::path& array_path, const std::vector<std::string>& values) {
    size_t max_length = 0;
    for (const auto& value : values) {
        max_length = std::max(max_length, value.size());
    }
    const size_t length_bytes = (max_length + 1) * sizeof(uint32_t);
    WriteJson(array_path / "zarr.json",
        {
            {"zarr_format", 3},
            {"node_type", "array"},
            {"shape", {values.size()}},
            {"data_type", {{"name", "fixed_length_utf32"}, {"configuration", {{"length_bytes", length_bytes}}}}},
            {"chunk_grid", {{"name", "regular"}, {"configuration", {{"chunk_shape", {values.size()}}}}}},
            {"chunk_key_encoding", {{"name", "default"}, {"configuration", {{"separator", "/"}}}}},
            {"codecs", {{{"name", "bytes"}, {"configuration", {{"endian", "little"}}}}}},
        });

    std::vector<uint8_t> bytes(values.size() * length_bytes, 0);
    for (size_t element = 0; element < values.size(); ++element) {
        for (size_t i = 0; i < values[element].size(); ++i) {
            bytes[(element * length_bytes) + (i * sizeof(uint32_t))] = static_cast<uint8_t>(values[element][i]);
        }
    }
    std::filesystem::create_directories(array_path / "c");
    std::ofstream chunk(array_path / "c" / "0", std::ios::binary);
    ASSERT_TRUE(chunk.is_open());
    chunk.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

nlohmann::json ImageMetadata(
    nlohmann::json shape = {1, 2, 3, 4, 5}, nlohmann::json dimension_names = {"time", "frequency", "polarization", "l", "m"}) {
    return {
        {"zarr_format", 3},
        {"node_type", "array"},
        {"shape", std::move(shape)},
        {"dimension_names", std::move(dimension_names)},
        {"data_type", "float32"},
    };
}

std::filesystem::path XradioFixturePath() {
    return TestRoot() / "data" / "images" / "zarr" / "xradio" / "minimal";
}

int64_t DirectorySize(const std::filesystem::path& path) {
    int64_t size = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            size += static_cast<int64_t>(entry.file_size());
        }
    }
    return size;
}

std::string Trim(std::string value) {
    const size_t first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = value.find_last_not_of(' ');
    return value.substr(first, last - first + 1);
}

std::string ParseFitsString(const std::string& value) {
    std::string parsed_value;
    for (size_t i = 1; i < value.size(); ++i) {
        if (value[i] != '\'') {
            parsed_value.push_back(value[i]);
        } else if (i + 1 < value.size() && value[i + 1] == '\'') {
            parsed_value.push_back('\'');
            ++i;
        } else {
            break;
        }
    }
    return Trim(std::move(parsed_value));
}

std::map<std::string, std::string> ParseFitsHeaders(const casacore::Vector<casacore::String>& headers) {
    std::map<std::string, std::string> values;
    for (const auto& header : headers) {
        const std::string record(header.c_str());
        const size_t equals = record.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = Trim(record.substr(0, equals));
        std::string value = Trim(record.substr(equals + 1));
        if (!value.empty() && value.front() == '\'') {
            value = ParseFitsString(value);
        } else {
            std::istringstream stream(value);
            stream >> value;
        }
        values[key] = value;
    }
    return values;
}

double HeaderDouble(const std::map<std::string, std::string>& headers, const std::string& key) {
    return std::stod(headers.at(key));
}

std::map<std::string, std::string> StorageInfoMap(const std::vector<std::pair<std::string, std::string>>& storage_info) {
    return {storage_info.begin(), storage_info.end()};
}

class ZarrImageMetadataTest : public ::testing::Test {
protected:
    void SetUp() override {
        _store_path = TestRoot() / "data" / "generated" / "invalid-metadata.zarr";
        std::filesystem::remove_all(_store_path);
        WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    }

    void TearDown() override {
        std::filesystem::remove_all(_store_path);
    }

    bool InitializeWith(const nlohmann::json& metadata) {
        WriteJson(_store_path / "SKY" / "zarr.json", metadata);
        carta::ZarrImage image(_store_path.string());
        return image.Initialize();
    }

    std::filesystem::path _store_path;
};

TEST_F(ZarrImageMetadataTest, RejectsDimensionNamesAndShapeSizeMismatch) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, 4}, {"time", "frequency", "polarization", "l", "m"})));
}

TEST_F(ZarrImageMetadataTest, RejectsDuplicateDimensionNames) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, 4, 5, 6}, {"time", "frequency", "polarization", "l", "m", "l"})));
}

TEST_F(ZarrImageMetadataTest, RejectsMissingRequiredAxis) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, 4, 5}, {"time", "frequency", "polarization", "l", "other"})));
}

TEST_F(ZarrImageMetadataTest, RejectsMultipleTimePlanes) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({2, 2, 3, 4, 5})));
}

TEST_F(ZarrImageMetadataTest, RejectsNegativeShapeDimension) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, -4, 5})));
}

TEST_F(ZarrImageMetadataTest, RejectsShapeDimensionLargerThanInt) {
    const int64_t oversized_dimension = static_cast<int64_t>(std::numeric_limits<int>::max()) + 1;
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, oversized_dimension, 5})));
}

TEST_F(ZarrImageMetadataTest, RejectsNonIntegerShapeDimension) {
    EXPECT_FALSE(InitializeWith(ImageMetadata({1, 2, 3, 4.5, 5})));
}

TEST_F(ZarrImageMetadataTest, RejectsUnknownStokesLabel) {
    WriteJson(_store_path / "SKY" / "zarr.json", ImageMetadata({1, 2, 1, 4, 5}));
    WriteFixedLengthUtf32Strings(_store_path / "polarization", {"BOGUS"});

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());
    EXPECT_TRUE(image.GetStokesTypes().empty());

    const auto headers = ParseFitsHeaders(image.FitsHeaderStrings());
    EXPECT_EQ(headers.count("CRVAL4"), 0);
}

class ZarrImageDegradationTest : public ::testing::Test {
protected:
    void SetUp() override {
        _store_path = TestRoot() / "data" / "generated" / "zarr-degradation.zarr";
        _escape_path = TestRoot() / "data" / "generated" / "escape";
        std::filesystem::remove_all(_store_path);
        std::filesystem::remove_all(_escape_path);
        std::filesystem::copy(XradioFixturePath(), _store_path, std::filesystem::copy_options::recursive);
    }

    void TearDown() override {
        std::filesystem::remove_all(_store_path);
        std::filesystem::remove_all(_escape_path);
    }

    std::filesystem::path _store_path;
    std::filesystem::path _escape_path;
};

TEST_F(ZarrImageDegradationTest, InitializesWithoutCoordinateSystemInfo) {
    auto metadata = ReadJson(_store_path / "zarr.json");
    metadata["attributes"].erase("coordinate_system_info");
    WriteJson(_store_path / "zarr.json", metadata);

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    const auto headers = ParseFitsHeaders(image.FitsHeaderStrings());
    EXPECT_EQ(headers.count("CTYPE1"), 0);
    EXPECT_EQ(headers.count("CRVAL1"), 0);
}

TEST_F(ZarrImageDegradationTest, InitializesWithoutBeamArrayName) {
    auto metadata = ReadJson(_store_path / "SKY" / "zarr.json");
    metadata["attributes"].erase("beam_fit_params");
    WriteJson(_store_path / "SKY" / "zarr.json", metadata);

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    casacore::ImageBeamSet beams;
    EXPECT_FALSE(image.GetBeams(beams));
    EXPECT_EQ(ParseFitsHeaders(image.FitsHeaderStrings()).count("CASAMBM"), 0);
}

TEST_F(ZarrImageDegradationTest, IgnoresBeamArrayWithoutUnits) {
    auto metadata = ReadJson(_store_path / "BEAM" / "zarr.json");
    metadata["attributes"].erase("units");
    WriteJson(_store_path / "BEAM" / "zarr.json", metadata);

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    casacore::ImageBeamSet beams;
    EXPECT_FALSE(image.GetBeams(beams));
    EXPECT_EQ(ParseFitsHeaders(image.FitsHeaderStrings()).count("CASAMBM"), 0);
}

TEST_F(ZarrImageDegradationTest, RejectsBeamArrayNameOutsideStore) {
    std::filesystem::copy(_store_path / "BEAM", _escape_path, std::filesystem::copy_options::recursive);
    auto metadata = ReadJson(_store_path / "SKY" / "zarr.json");
    metadata["attributes"]["beam_fit_params"] = "../escape";
    WriteJson(_store_path / "SKY" / "zarr.json", metadata);

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    casacore::ImageBeamSet beams;
    EXPECT_FALSE(image.GetBeams(beams));
    EXPECT_EQ(ParseFitsHeaders(image.FitsHeaderStrings()).count("CASAMBM"), 0);
}

TEST_F(ZarrImageDegradationTest, OmitsStokesIncrementForNonUniformLabels) {
    auto metadata = ReadJson(_store_path / "SKY" / "zarr.json");
    metadata["shape"][2] = 3;
    metadata["attributes"].erase("beam_fit_params");
    WriteJson(_store_path / "SKY" / "zarr.json", metadata);
    WriteFixedLengthUtf32Strings(_store_path / "polarization", {"I", "Q", "V"});

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    const auto stokes_types = image.GetStokesTypes();
    ASSERT_EQ(stokes_types.size(), 3);
    EXPECT_EQ(stokes_types[0], casacore::Stokes::I);
    EXPECT_EQ(stokes_types[1], casacore::Stokes::Q);
    EXPECT_EQ(stokes_types[2], casacore::Stokes::V);

    const auto headers = ParseFitsHeaders(image.FitsHeaderStrings());
    EXPECT_EQ(headers.at("CTYPE4"), "STOKES");
    EXPECT_EQ(headers.count("CDELT4"), 0);
}

TEST_F(ZarrImageDegradationTest, FormatsArbitraryMetadataAsValidFitsCards) {
    auto metadata = ReadJson(_store_path / "SKY" / "zarr.json");
    metadata["attributes"]["object_name"] = "O'Brien";
    metadata["attributes"]["observer"] = std::string(100, 'A');
    metadata["attributes"]["user"]["invalid key"] = "ignored";
    WriteJson(_store_path / "SKY" / "zarr.json", metadata);

    carta::ZarrImage image(_store_path.string());
    ASSERT_TRUE(image.Initialize());

    const auto header_strings = image.FitsHeaderStrings();
    for (const auto& header : header_strings) {
        EXPECT_EQ(header.size(), 80);
    }

    const auto headers = ParseFitsHeaders(header_strings);
    EXPECT_EQ(headers.at("OBJECT"), "O'Brien");
    EXPECT_EQ(headers.at("OBSERVER"), std::string(68, 'A'));
    EXPECT_EQ(headers.count("INVALID"), 0);

    EXPECT_NO_THROW(carta::CartaZarrImage(_store_path.string()));
}

TEST(ZarrImageSizeTest, UsesExactDirectorySizeWhenWalkCompletes) {
    const auto fixture_path = XradioFixturePath();
    int64_t size = -1;
    bool size_is_upper_bound = true;

    ASSERT_TRUE(carta::ZarrImage::ComputeImageDataSizeBytes(fixture_path.string(), size, size_is_upper_bound));
    EXPECT_EQ(size, DirectorySize(fixture_path));
    EXPECT_FALSE(size_is_upper_bound);

    CARTA::FileInfo file_info;
    carta::FileInfoLoader loader(fixture_path.string());
    ASSERT_TRUE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::ZARR);
    EXPECT_EQ(file_info.size(), size);
    EXPECT_FALSE(file_info.size_is_upper_bound());
}

TEST(ZarrImageSizeTest, UsesMetadataUpperBoundWhenDirectoryWalkTimesOut) {
    const auto fixture_path = XradioFixturePath();
    carta::ZarrStore store(fixture_path.string());
    ASSERT_TRUE(store.Open());
    const int64_t metadata_size = store.ComputeTotalArraySizeBytes();

    int64_t size = -1;
    bool size_is_upper_bound = false;
    ASSERT_TRUE(carta::ZarrImage::ComputeImageDataSizeBytes(
        fixture_path.string(), size, size_is_upper_bound, std::chrono::milliseconds(0)));
    EXPECT_EQ(size, metadata_size);
    EXPECT_TRUE(size_is_upper_bound);
}

TEST(ZarrLoaderIntegrationTest, DispatchesAndOpensDefaultAndExplicitSkyArray) {
    auto loader = carta::FileLoader::GetLoader(XradioFixturePath().string());
    ASSERT_NE(loader, nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<carta::ZarrLoader>(loader), nullptr);

    EXPECT_NO_THROW(loader->OpenFile(""));
    EXPECT_EQ(loader->GetShape(), casacore::IPosition(std::vector<int>{4, 5, 3, 2}));

    EXPECT_NO_THROW(loader->OpenFile("SKY"));
    EXPECT_EQ(loader->GetShape(), casacore::IPosition(std::vector<int>{4, 5, 3, 2}));
}

TEST(ZarrImageMetadataIntegrationTest, InitializesAndReordersAxesFromRealStore) {
    carta::ZarrImage image(XradioFixturePath().string());

    ASSERT_TRUE(image.Initialize());
    EXPECT_EQ(image.GetDataType(), casacore::TpFloat);
    EXPECT_EQ(image.GetShape(), casacore::IPosition(std::vector<int>{4, 5, 3, 2}));

    const auto& axes = image.GetAxes();
    ASSERT_EQ(axes.size(), 5);
    EXPECT_EQ(axes.at("time").index, 0);
    EXPECT_EQ(axes.at("frequency").index, 1);
    EXPECT_EQ(axes.at("polarization").index, 2);
    EXPECT_EQ(axes.at("l").index, 3);
    EXPECT_EQ(axes.at("m").index, 4);
}

TEST(ZarrImageMetadataIntegrationTest, BuildsFitsHeadersStokesAndStorageInfo) {
    carta::ZarrImage image(XradioFixturePath().string());
    ASSERT_TRUE(image.Initialize());

    const auto headers = ParseFitsHeaders(image.FitsHeaderStrings());
    EXPECT_EQ(headers.at("SIMPLE"), "T");
    EXPECT_EQ(headers.at("BITPIX"), "-32");
    EXPECT_EQ(headers.at("NAXIS"), "4");
    EXPECT_EQ(headers.at("NAXIS1"), "4");
    EXPECT_EQ(headers.at("NAXIS2"), "5");
    EXPECT_EQ(headers.at("NAXIS3"), "3");
    EXPECT_EQ(headers.at("NAXIS4"), "2");
    EXPECT_EQ(headers.at("CTYPE1"), "RA---SIN");
    EXPECT_EQ(headers.at("CTYPE2"), "DEC--SIN");
    EXPECT_EQ(headers.at("CTYPE3"), "FREQ");
    EXPECT_EQ(headers.at("CTYPE4"), "STOKES");
    EXPECT_EQ(headers.at("RADESYS"), "FK5");
    EXPECT_EQ(headers.at("CUNIT1"), "deg");
    EXPECT_EQ(headers.at("CUNIT2"), "deg");
    EXPECT_EQ(headers.at("CUNIT3"), "Hz");
    EXPECT_EQ(headers.at("SPECSYS"), "LSRK");
    EXPECT_EQ(headers.at("BUNIT"), "Jy/beam");
    EXPECT_EQ(headers.at("OBJECT"), "Zarr test source");
    EXPECT_EQ(headers.at("TIMESYS"), "UTC");
    EXPECT_EQ(headers.at("CASAMBM"), "T");
    EXPECT_NEAR(HeaderDouble(headers, "CRPIX1"), 2.0, 1e-12);
    EXPECT_NEAR(HeaderDouble(headers, "CRPIX2"), 3.0, 1e-12);
    EXPECT_NEAR(HeaderDouble(headers, "CRVAL3"), 1.4e9, 1.0);
    EXPECT_NEAR(HeaderDouble(headers, "CDELT3"), 1.0e6, 1.0);
    EXPECT_NEAR(HeaderDouble(headers, "MJD-OBS"), 59000.0, 1e-9);

    const auto stokes_types = image.GetStokesTypes();
    ASSERT_EQ(stokes_types.size(), 2);
    EXPECT_EQ(stokes_types[0], casacore::Stokes::I);
    EXPECT_EQ(stokes_types[1], casacore::Stokes::Q);

    const auto storage_info = StorageInfoMap(image.GetStorageInfo());
    EXPECT_EQ(storage_info.at("Chunk shape"), "[2, 5, 1, 1] (RA, DEC, FREQ, STOKES)");
    EXPECT_EQ(storage_info.at("Compressor"), "zstd (level 1)");
}

TEST(ZarrImageMetadataIntegrationTest, BuildsCasacoreCoordinateSystemAndBeams) {
    carta::CartaZarrImage image(XradioFixturePath().string());

    ASSERT_TRUE(image.ok());
    EXPECT_EQ(image.shape(), casacore::IPosition(std::vector<int>{4, 5, 3, 2}));
    EXPECT_EQ(image.units().getName(), "Jy/beam");
    EXPECT_GE(image.coordinates().directionCoordinateNumber(), 0);
    EXPECT_GE(image.coordinates().spectralCoordinateNumber(), 0);
    EXPECT_GE(image.coordinates().polarizationCoordinateNumber(), 0);

    const casacore::ImageBeamSet beam_set = image.imageInfo().getBeamSet();
    ASSERT_EQ(beam_set.nchan(), 3);
    ASSERT_EQ(beam_set.nstokes(), 2);
    const casacore::GaussianBeam& beam = beam_set.getBeam(2, 1);
    EXPECT_NEAR(beam.getMajor(casacore::Unit("rad")), 2.21e-5, 1e-12);
    EXPECT_NEAR(beam.getMinor(casacore::Unit("rad")), 1.105e-5, 1e-12);
    EXPECT_NEAR(beam.getPA(casacore::Unit("rad")), 0.12, 1e-12);
}

} // namespace
