/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Timer/Timer.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

struct ContourTestParams {
    fs::path image_file;
    CARTA::SmoothingMode mode;
    fs::path contour_dir;
};

class ContourTest : public ::testing::TestWithParam<ContourTestParams> {
public:
    struct ContourMetadata {
        std::vector<int32_t> indices;
        std::vector<int32_t> levels;
        int32_t decimation_factor;
        int32_t uncompressed_coordinates_size;

        ContourMetadata(const std::string& file_path) {
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

            indices.resize(num_levels);
            levels.resize(num_levels);
            if (num_levels > 0) {
                if (!file.read(reinterpret_cast<char*>(indices.data()), num_levels * sizeof(int32_t))) {
                    throw std::runtime_error("Failed to read contour indices array from: " + file_path);
                }
                if (!file.read(reinterpret_cast<char*>(levels.data()), num_levels * sizeof(int32_t))) {
                    throw std::runtime_error("Failed to read level values array from: " + file_path);
                }
            }

            if (!file.read(reinterpret_cast<char*>(&decimation_factor), sizeof(decimation_factor)) ||
                !file.read(reinterpret_cast<char*>(&uncompressed_coordinates_size), sizeof(uncompressed_coordinates_size))) {
                throw std::runtime_error("Failed to read contour metadata trailer from: " + file_path);
            }
        }
    };

    struct LevelMetadata {
        int32_t level;
        uint32_t num_contours;
        std::vector<uint64_t> indices;

        LevelMetadata(const std::string& file_path) {
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("Could not open contour metadata file at: " + file_path);
            }

            // Read contour level (int32_t, '<i')
            if (!file.read(reinterpret_cast<char*>(&level), sizeof(level))) {
                throw std::runtime_error("Failed to read contour level from: " + file_path);
            }

            // Read number of contour boundaries (uint32_t, '<I')
            if (!file.read(reinterpret_cast<char*>(&num_contours), sizeof(num_contours))) {
                throw std::runtime_error("Failed to read number of contours from: " + file_path);
            }

            // Read each index boundary (uint64_t, '<Q') for num_contours elements
            indices.resize(num_contours);
            if (num_contours > 0) {
                if (!file.read(reinterpret_cast<char*>(indices.data()), num_contours * sizeof(uint64_t))) {
                    throw std::runtime_error("Failed to read contour index boundaries from: " + file_path);
                }
            }
        }
    };

    struct Point {
        double x;
        double y;

        Point(double x_val, double y_val) : x(x_val), y(y_val) {}
    };

    static std::vector<Point> ToPoints(const std::vector<float>& values) {
        std::vector<Point> points;
        points.reserve(values.size() / 2);
        for (size_t i = 0; i + 1 < values.size(); i += 2) {
            points.emplace_back(static_cast<double>(values[i]), static_cast<double>(values[i + 1]));
        }
        return points;
    }

    static std::vector<Point> ReadContourData(const std::string& file_path) {
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

        std::vector<Point> coordinates;
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
    fs::path contour_meta_file = params.contour_dir / (stem + "_metadata.bin");
    ContourMetadata contour_metadata(contour_meta_file.string());
    const std::vector<int32_t>& levels = contour_metadata.levels;

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.image_file));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

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
    set_contour_params.set_decimation_factor(contour_metadata.decimation_factor);
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
        int32_t index = contour_metadata.indices[level_position];

        // get level metadata
        fs::path index_dir = params.contour_dir / std::to_string(index);
        fs::path meta_file = index_dir / (std::to_string(index) + "_metadata.bin");
        LevelMetadata level_metadata(meta_file.string());

        // get expected coordinates
        fs::path data_file = index_dir / (std::to_string(index) + "_data.bin");
        auto expected = ReadContourData(data_file.string());

        // format generated vertices into (x,y) pairs
        const auto& gen_verts = generated_vertices[level];
        auto gen_coords = ToPoints(gen_verts);

        EXPECT_EQ(expected.size(), gen_coords.size()) << "Vertex count mismatch for level " << level;

        const double tolerance = 0.13; // Allow small floating-point differences
        for (size_t i = 0; i < std::min(expected.size(), gen_coords.size()); ++i) {
            EXPECT_NEAR(expected[i].x, gen_coords[i].x, tolerance) << "X coordinate mismatch at vertex " << i << " for level " << level;
            EXPECT_NEAR((expected[i].y), gen_coords[i].y, tolerance) << "Y coordinate mismatch at vertex " << i << " for level " << level;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(AllModesAndFormats, ContourTest,
    ::testing::Values(ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::NoSmoothing,
                          ContourData() / "sensible-picture-noisey-none"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-block"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-gaussian"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::NoSmoothing,
            ContourData() / "sensible-picture-noisey-nans-none"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-nans-block"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-nans-gaussian"}));
