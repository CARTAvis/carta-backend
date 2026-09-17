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
    fs::path contour_vertices_dir;
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

    void SaveVerticesToBinary(const std::string& filepath, const std::vector<float>& vertices) {
        std::ofstream outfile(filepath, std::ios::binary | std::ios::out | std::ios::trunc);
        
        if (!outfile.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filepath);
        }

        uint64_t count = static_cast<uint64_t>(vertices.size());
        outfile.write(reinterpret_cast<const char*>(&count), sizeof(uint64_t));
        
        outfile.write(reinterpret_cast<const char*>(vertices.data()), count * sizeof(float));
        outfile.close();
        
        std::ifstream check(filepath, std::ios::binary);
        uint64_t verify_count = 0;
        check.read(reinterpret_cast<char*>(&verify_count), sizeof(uint64_t));
        cout << "VERIFY: Wrote " << count << " vertices to " << filepath << ". Header read back as: " << verify_count << endl;
    }

    static std::vector<std::pair<double, double>> ReadBinaryContours(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file at: " + file_path);
        }

        file.seekg(0, std::ios::beg);

        file.clear();
        file.seekg(0, std::ios::beg);

        uint8_t buffer[8];
        file.read(reinterpret_cast<char*>(buffer), 8);

        uint64_t header_count = 0;
        for (int i = 0; i < 8; ++i) {
            header_count |= (static_cast<uint64_t>(buffer[i]) << (8 * i));
        }

        // cout << "Header count: " << header_count << endl;

        if (header_count > 10000000) {
            throw std::runtime_error("Header alignment error! Read: " + std::to_string(header_count));
        }

        std::vector<float> values(header_count);
        if (!file.read(reinterpret_cast<char*>(values.data()), header_count * sizeof(float))) {
            throw std::runtime_error("Failed to read float data block from: " + file_path);
        }

        std::vector<std::pair<double, double>> coordinates;
        coordinates.reserve(header_count / 2);
        for (uint64_t i = 0; i + 1 < header_count; i += 2) {
            coordinates.emplace_back(static_cast<double>(values[i]),
                                    static_cast<double>(values[i + 1]));
        }
        return coordinates;
    }

    std::map<double, std::vector<std::pair<double, double>>> LoadVertices(const fs::path& dir, const std::vector<double>& levels) {
        std::map<double, std::vector<std::pair<double, double>>> expected;
        for (double level : levels) {
            fs::path bin_file = dir / ("level_" + LevelToString(level) + ".bin");
            expected[level] = ReadBinaryContours(bin_file.string());
        }
        return expected;
    }

protected:
    std::mutex _callback_mutex;
};

class ContourDeterminismTest : public ::testing::TestWithParam<ContourParams> {
public:
    static std::string LevelToString(double level) {
        if (std::floor(level) == level) {
            return std::to_string(static_cast<int>(level));
        }
        std::ostringstream ss;
        ss << level;
        return ss.str();
    }

protected:
    std::mutex _callback_mutex;
};

TEST_P(ContourTest, VerifyVertices) {
    const auto& params = GetParam();

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.image_file));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    std::vector<double> levels{0, -1, 1};
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

    // for (auto const& [level, vertices] : generated_vertices) {
        // SaveVerticesToBinary("level_" + std::to_string(level) + ".bin", vertices);
    // }

    auto expected = LoadVertices(params.contour_vertices_dir, levels);

    for (double level : levels) {
        auto& gen_verts = generated_vertices[level];
        std::vector<std::pair<double, double>> gen_coords;
        for (size_t i = 0; i < gen_verts.size() / 2; ++i) {
            gen_coords.emplace_back(gen_verts[i * 2], gen_verts[i * 2 + 1]);
        }

        EXPECT_EQ(expected[level].size(), gen_coords.size()) << "Vertex count mismatch for level " << level;

        const double tolerance = 0.1; // Allow small floating-point differences
        for (size_t i = 0; i < std::min(expected[level].size(), gen_coords.size()); ++i) {
            EXPECT_NEAR(expected[level][i].first, gen_coords[i].first, tolerance)
                << "X coordinate mismatch at vertex " << i << " for level " << level;
            EXPECT_NEAR((expected[level][i].second), gen_coords[i].second, tolerance)
                << "Y coordinate mismatch at vertex " << i << " for level " << level;
        }
    }
}

TEST_P(ContourDeterminismTest, DeterminismTest) {
    const auto& params = GetParam();

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.image_file));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    std::vector<double> levels{0, -1, 1};
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

    const int num_runs = 5;
    std::vector<std::map<double, std::vector<float>>> all_runs_vertices;

    for (int run = 0; run < num_runs; ++run) {
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

        std::map<double, std::vector<float>> run_map;
        for (auto level : levels) {
            run_map[level] = generated_vertices[level];
        }
        all_runs_vertices.push_back(run_map);
        spdlog::info("Completed contour run {}/{}", run + 1, num_runs);
    }

    for (double level : levels) {
        for (int run = 1; run < num_runs; ++run) {
            const auto& first_run = all_runs_vertices[0][level];
            const auto& current_run = all_runs_vertices[run][level];

            EXPECT_EQ(first_run.size(), current_run.size())
                << "Vertex count mismatch for level " << level << " between run 1 and run " << (run + 1);

            if (first_run.size() == current_run.size()) {
                for (size_t i = 0; i < first_run.size(); ++i) {
                    EXPECT_FLOAT_EQ(first_run[i], current_run[i])
                        << "Vertex value mismatch at index " << i << " for level " << level
                        << " between run 1 and run " << (run + 1);
                }
            }
        }
        spdlog::info("Level {}: Determinism verified across {} runs", level, num_runs);
    }
}

INSTANTIATE_TEST_SUITE_P(AllModesAndFormats, ContourTest,
    ::testing::Values(ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_contours"},
        ContourParams{FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_nans_contours"},
        ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_gaussian_contours"},
        ContourParams{
            FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_nans_gaussian_contours"},
        ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_block_contours"},
        ContourParams{
            FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_nans_block_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_contours"},
        ContourParams{Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_nans_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_gaussian_contours"},
        ContourParams{
            Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_nans_gaussian_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_block_contours"},
        ContourParams{
            Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_nans_block_contours"}
        ));

INSTANTIATE_TEST_SUITE_P(DeterminismAllModes, ContourDeterminismTest,
    ::testing::Values(ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_contours"},
        ContourParams{FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_nans_contours"},
        ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_gaussian_contours"},
        ContourParams{
            FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_nans_gaussian_contours"},
        ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_block_contours"},
        ContourParams{
            FitsImages() / "500x500_nans.fits", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_nans_block_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_contours"},
        ContourParams{Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_nans_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_gaussian_contours"},
        ContourParams{
            Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::GaussianBlur, ContourData() / "500x500_nans_gaussian_contours"},
        ContourParams{Hdf5Images() / "500x500.hdf5", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_block_contours"},
        ContourParams{
            Hdf5Images() / "500x500_nans.hdf5", CARTA::SmoothingMode::BlockAverage, ContourData() / "500x500_nans_block_contours"}
        ));

