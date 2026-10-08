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
        uint32_t num_levels;

        ContourMetadata(const std::string& file_path) {
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("Could not open top metadata file at: " + file_path);
            }

            // Read number of levels (uint32_t, '<I')
            if (!file.read(reinterpret_cast<char*>(&num_levels), sizeof(num_levels))) {
                throw std::runtime_error("Failed to read number of contours from: " + file_path);
            }
        }
    };

    struct LevelMetadata {
        float level;
        uint32_t num_points;
        uint32_t num_indices;
        std::vector<uint32_t> indices;

        LevelMetadata(const std::string& file_path) {
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("Could not open contour metadata file at: " + file_path);
            }

            // Read level (int32_t, '<f')
            file.read(reinterpret_cast<char*>(&level), sizeof(level));
            // Read number of points (uint32_t, '<I')
            file.read(reinterpret_cast<char*>(&num_points), sizeof(num_points));
            // Read number of indices (uint32_t, '<I')
            file.read(reinterpret_cast<char*>(&num_indices), sizeof(num_indices));
            // Read raw_start_indices
            indices.resize(num_indices);
            file.read(reinterpret_cast<char*>(indices.data()), num_indices * sizeof(uint32_t));
        }
    };

    struct Point {
        float x;
        float y;

        Point(float x_val, float y_val) : x(x_val), y(y_val) {}
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
            coordinates.emplace_back(Point(static_cast<float>(values[i]), static_cast<float>(values[i + 1])));
        }
        return coordinates;
    }

protected:
    std::mutex _callback_mutex;
};

TEST_P(ContourTest, VerifyVertices) {
    const auto& params = GetParam();

    std::string stem = params.contour_dir.stem().string();
    fs::path contour_meta_file = params.contour_dir / (stem + "_metadata.bin");
    ContourMetadata contour_metadata(contour_meta_file.string());
    const uint32_t& num_levels = contour_metadata.num_levels;

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
    set_contour_params.set_smoothing_mode(params.mode);
    set_contour_params.set_smoothing_factor(4);
    set_contour_params.set_decimation_factor(4);
    set_contour_params.set_compression_level(8);
    set_contour_params.set_contour_chunk_size(100000);

    EXPECT_TRUE(frame->SetContourParameters(set_contour_params));

    // generate and load contour vertices by level
    for (size_t level_position = 0; level_position < num_levels; ++level_position) {
        // get level metadata
        fs::path index_dir = params.contour_dir / std::to_string(level_position);
        fs::path meta_file = index_dir / (std::to_string(level_position) + "_metadata.bin");
        LevelMetadata level_metadata(meta_file.string());

        // get expected coordinates
        fs::path data_file = index_dir / (std::to_string(level_position) + "_data.bin");
        auto expected = ReadContourData(data_file.string());

        std::vector<float> generated_vertices;

        set_contour_params.clear_levels();
        set_contour_params.add_levels(level_metadata.level);

        EXPECT_TRUE(frame->SetContourParameters(set_contour_params));

        auto callback = [&](double cb_level, double progress, const std::vector<float>& vertices, const std::vector<int>& indices) {
            std::unique_lock<std::mutex> lock(_callback_mutex);
            if (cb_level == level_metadata.level) {
                generated_vertices.insert(generated_vertices.end(), vertices.begin(), vertices.end());
            }
        };

        EXPECT_TRUE(frame->ContourImage(callback, frame->CurrentZ()));

        // format generated vertices into (x,y) pairs
        auto gen_coords = ToPoints(generated_vertices);

        EXPECT_EQ(expected.size(), gen_coords.size()) << "Vertex count mismatch for level " << level_metadata.level;

        const double tolerance = 0.1; // Allow small floating-point differences
        for (size_t i = 0; i < std::min(expected.size(), gen_coords.size()); ++i) {
            EXPECT_NEAR(expected[i].x, gen_coords[i].x, tolerance)
                << "X coordinate mismatch at vertex " << i << " for level " << level_metadata.level;
            EXPECT_NEAR((expected[i].y), gen_coords[i].y, tolerance)
                << "Y coordinate mismatch at vertex " << i << " for level " << level_metadata.level;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(AllModesAndFormats, ContourTest,
    ::testing::Values(ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::NoSmoothing,
                          ContourData() / "sensible-picture-noisey-NoSmoothing"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-BlockAverage"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-GaussianBlur"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::NoSmoothing,
            ContourData() / "sensible-picture-noisey-nans-NoSmoothing"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::BlockAverage,
            ContourData() / "sensible-picture-noisey-nans-BlockAverage"},
        ContourTestParams{FitsImages() / "sensible-picture-noisey-nans.fits", CARTA::SmoothingMode::GaussianBlur,
            ContourData() / "sensible-picture-noisey-nans-GaussianBlur"}));
