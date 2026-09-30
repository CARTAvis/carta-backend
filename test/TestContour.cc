/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Timer/Timer.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

struct ContourParams {
    fs::path image_file;
    CARTA::SmoothingMode mode;
    fs::path contour_dir;
};

class ContourTest : public ::testing::TestWithParam<ContourParams> {
public:
    static std::string LevelToString(double level) {
        if (std::floor(level) == level) {
            return std::to_string(static_cast<int>(level));
        }
        std::ostringstream ss;
        ss << level;
        return ss.str();
    }

    struct TopMetadata {
        std::vector<int32_t> indices;
        std::vector<int32_t> levels;
        int32_t decimation_factor;
        int32_t uncompressed_coordinates_size;
    };

    static TopMetadata ReadTopMetadata(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open top metadata file at: " + file_path);
        }

        int32_t num_levels = 0;
        if (!file.read(reinterpret_cast<char*>(&num_levels), sizeof(num_levels))) {
            throw std::runtime_error("Failed to read number of levels from: " + file_path);
        }

        if (num_levels < 0) {
            throw std::runtime_error("Invalid negative level count in metadata: " + std::to_string(num_levels));
        }

        TopMetadata metadata;
        metadata.indices.resize(num_levels);
        metadata.levels.resize(num_levels);
        if (num_levels > 0) {
            if (!file.read(reinterpret_cast<char*>(metadata.indices.data()), num_levels * sizeof(int32_t))) {
                throw std::runtime_error("Failed to read contour indices array from: " + file_path);
            }
            if (!file.read(reinterpret_cast<char*>(metadata.levels.data()), num_levels * sizeof(int32_t))) {
                throw std::runtime_error("Failed to read level values array from: " + file_path);
            }
        }

        if (!file.read(reinterpret_cast<char*>(&metadata.decimation_factor), sizeof(metadata.decimation_factor)) ||
            !file.read(reinterpret_cast<char*>(&metadata.uncompressed_coordinates_size), sizeof(metadata.uncompressed_coordinates_size))) {
            throw std::runtime_error("Failed to read contour metadata trailer from: " + file_path);
        }

        return metadata;
    }

    struct ContourMetadata {
        int32_t level;
        uint32_t num_contours;
        std::vector<uint64_t> indices;
    };

    static ContourMetadata ReadContourMetadata(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open contour metadata file at: " + file_path);
        }

        ContourMetadata meta;

        // Read contour level (int32_t, '<i')
        if (!file.read(reinterpret_cast<char*>(&meta.level), sizeof(meta.level))) {
            throw std::runtime_error("Failed to read contour level from: " + file_path);
        }

        // Read number of contour boundaries (uint32_t, '<I')
        if (!file.read(reinterpret_cast<char*>(&meta.num_contours), sizeof(meta.num_contours))) {
            throw std::runtime_error("Failed to read number of contours from: " + file_path);
        }

        // Read each index boundary (uint64_t, '<Q') for num_contours elements
        meta.indices.resize(meta.num_contours);
        if (meta.num_contours > 0) {
            if (!file.read(reinterpret_cast<char*>(meta.indices.data()), meta.num_contours * sizeof(uint64_t))) {
                throw std::runtime_error("Failed to read contour index boundaries from: " + file_path);
            }
        }

        return meta;
    }

    static std::vector<std::pair<double, double>> ReadContourData(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file at: " + file_path);
        }

        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file_size % sizeof(float) != 0) {
            throw std::runtime_error("File size is not a multiple of float size: " + file_path);
        }

        size_t num_floats = file_size / sizeof(float);
        if (num_floats % 2 != 0) {
            throw std::runtime_error("Number of float values is odd, expected (x, y) pairs: " + file_path);
        }

        std::vector<float> values(num_floats);
        if (!file.read(reinterpret_cast<char*>(values.data()), file_size)) {
            throw std::runtime_error("Failed to read float data block from: " + file_path);
        }

        std::vector<std::pair<double, double>> coordinates;
        coordinates.reserve(num_floats / 2);
        for (size_t i = 0; i < num_floats; i += 2) {
            coordinates.emplace_back(static_cast<double>(values[i]), static_cast<double>(values[i + 1]));
        }
        return coordinates;
    }

protected:
    std::mutex _callback_mutex;
};

TEST_P(ContourTest, VerifyVertices) {
    const auto& params = GetParam();

    // get levels from top metadata file
    std::string stem = params.contour_dir.stem().string();
    fs::path top_meta_file = params.contour_dir / (stem + "_metadata.bin");
    TopMetadata top_metadata = ReadTopMetadata(top_meta_file.string());
    const std::vector<int32_t>& levels = top_metadata.levels;

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.image_file));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    // std::vector<double> levels{0, -1, 1};
    CARTA::SetContourParameters set_contour_params;
    set_contour_params.set_file_id(0);
    set_contour_params.set_reference_file_id(0);
    auto* bounds = set_contour_params.mutable_image_bounds();
    bounds->set_x_min(0);
    bounds->set_x_max(frame->Width());
    bounds->set_y_min(0);
    bounds->set_y_max(frame->Height());
    *set_contour_params.mutable_levels() = {levels.begin(), levels.end()};
    set_contour_params.set_smoothing_mode(params.mode);
    set_contour_params.set_smoothing_factor(4);
    set_contour_params.set_decimation_factor(4);
    set_contour_params.set_compression_level(8);
    set_contour_params.set_contour_chunk_size(100000);

    EXPECT_TRUE(frame->SetContourParameters(set_contour_params));

    std::unordered_map<double, std::vector<float>> generated_vertices;
    for (auto level : levels) {
        generated_vertices[level] = {};
    }

    auto callback = [&](double level, double progress, const std::vector<float>& vertices, const std::vector<int>& indices) {
        std::unique_lock<std::mutex> lock(_callback_mutex);
        if (generated_vertices.count(level)) {
            generated_vertices[level].insert(generated_vertices[level].end(), vertices.begin(), vertices.end());
        }
    };

    {
        carta::Timer t_contour;
        EXPECT_TRUE(frame->ContourImage(callback, frame->CurrentZ()));
    }

    // load contour vertices by level
    for (size_t level_position = 0; level_position < levels.size(); ++level_position) {
        int32_t level = levels[level_position];
        int32_t index = top_metadata.indices[level_position];

        // get level metadata
        fs::path index_dir = params.contour_dir / std::to_string(index);
        fs::path meta_file = index_dir / (std::to_string(index) + "_metadata.bin");
        ContourMetadata metadata = ReadContourMetadata(meta_file.string());

        // get expected coordinates
        fs::path data_file = index_dir / (std::to_string(index) + "_data.bin");
        std::vector<std::pair<double, double>> expected = ReadContourData(data_file.string());

        auto& gen_verts = generated_vertices[level];
        std::vector<std::pair<double, double>> gen_coords;
        for (size_t i = 0; i < gen_verts.size() / 2; ++i) {
            gen_coords.emplace_back(gen_verts[i * 2], gen_verts[i * 2 + 1]);
        }

        EXPECT_EQ(expected.size(), gen_coords.size()) << "Vertex count mismatch for level " << level;

        const double tolerance = 0.13; // Allow small floating-point differences
        for (size_t i = 0; i < std::min(expected.size(), gen_coords.size()); ++i) {
            EXPECT_NEAR(expected[i].first, gen_coords[i].first, tolerance)
                << "X coordinate mismatch at vertex " << i << " for level " << level;
            EXPECT_NEAR((expected[i].second), gen_coords[i].second, tolerance)
                << "Y coordinate mismatch at vertex " << i << " for level " << level;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(AllModesAndFormats, ContourTest,
    ::testing::Values(ContourParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::NoSmoothing,
                          ContourData() / "sensible-picture-noisey-none"},
        ContourParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-block"},
        ContourParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-gaussian"},
        ContourParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::NoSmoothing,
            ContourData() / "sensible-picture-noisey-nans-none"},
        ContourParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-nans-block"},
        ContourParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-nans-gaussian"}));
