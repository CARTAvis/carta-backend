/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
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

    static std::vector<std::pair<double, double>> ReadBinaryContours(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open binary contour file: " + file_path);
        }

        std::vector<std::pair<double, double>> coordinates;
        while (true) {
            char magic[4];
            file.read(magic, 4);
            if (file.gcount() == 0 && file.eof()) {
                break; // finished reading all segments
            }
            if (file.gcount() != 4 || std::string(magic, 4) != "CTRN") {
                throw std::runtime_error("Invalid binary file: magic number mismatch (expected 'CTRN')");
            }

            uint32_t version;
            file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            if (!file.good() || version != 1) {
                throw std::runtime_error("Unsupported binary format version: " + std::to_string(version));
            }

            uint32_t num_coordinates;
            file.read(reinterpret_cast<char*>(&num_coordinates), sizeof(uint32_t));
            if (!file.good()) {
                throw std::runtime_error("Cannot read coordinate count from header");
            }

            char reserved[4];
            file.read(reserved, 4);
            if (!file.good()) {
                throw std::runtime_error("Cannot read reserved header bytes");
            }

            std::vector<float> float_data(num_coordinates * 2);
            if (num_coordinates > 0) {
                file.read(reinterpret_cast<char*>(float_data.data()), float_data.size() * sizeof(float));
                if (!file.good()) {
                    throw std::runtime_error("Cannot read coordinate data from file");
                }

                coordinates.reserve(coordinates.size() + num_coordinates);
                for (size_t i = 0; i < num_coordinates; ++i) {
                    coordinates.emplace_back(
                        static_cast<double>(float_data[i * 2]),
                        static_cast<double>(float_data[i * 2 + 1])
                    );
                }
            }
        }

        return coordinates;
    }

    static int ValidateBinaryContours(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return -1;
        }

        int total_coordinates = 0;
        while (true) {
            char magic[4];
            file.read(magic, 4);
            if (file.gcount() == 0 && file.eof()) {
                break;
            }
            if (file.gcount() != 4 || std::string(magic, 4) != "CTRN") {
                return -1;
            }

            uint32_t version;
            file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            if (!file.good() || version != 1) {
                return -1;
            }

            uint32_t num_coordinates;
            file.read(reinterpret_cast<char*>(&num_coordinates), sizeof(uint32_t));
            if (!file.good()) {
                return -1;
            }

            char reserved[4];
            file.read(reserved, 4);
            if (!file.good()) {
                return -1;
            }

            if (num_coordinates > 0) {
                file.seekg(static_cast<std::streamoff>(num_coordinates) * 2 * sizeof(float), std::ios::cur);
                if (!file.good()) {
                    return -1;
                }
                total_coordinates += static_cast<int>(num_coordinates);
            }
        }

        return total_coordinates;
    }

    std::map<double, std::vector<std::pair<double, double>>> LoadVertices(const fs::path& dir, const std::vector<double>& levels) {
    std::map<double, std::vector<std::pair<double, double>>> expected;
    for (double level : levels) {
        fs::path bin_file = dir / ("level_" + LevelToString(level) + ".bin");
        
        expected[level] = ReadBinaryContours(bin_file.string());
        
        spdlog::info("Successfully loaded binary contours: {}", bin_file.string());
    }
    return expected;
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

    EXPECT_TRUE(frame->ContourImage(callback, frame->CurrentZ()));

    auto expected = LoadVertices(params.contour_vertices_dir, levels);

    for (double level : levels) {
        auto& gen_verts = generated_vertices[level];
        std::vector<std::pair<double, double>> gen_coords;
        for (size_t i = 0; i < gen_verts.size() / 2; ++i) {
            gen_coords.emplace_back(gen_verts[i * 2], gen_verts[i * 2 + 1]);
        }

        EXPECT_EQ(expected[level].size(), gen_coords.size()) << "Vertex count mismatch for level " << level;

        const double tolerance = 0.01; // Allow small floating-point differences
        for (size_t i = 0; i < std::min(expected[level].size(), gen_coords.size()); ++i) {
            EXPECT_NEAR(expected[level][i].first, gen_coords[i].first, tolerance)
                << "X coordinate mismatch at vertex " << i << " for level " << level;
            EXPECT_NEAR(expected[level][i].second, gen_coords[i].second, tolerance)
                << "Y coordinate mismatch at vertex " << i << " for level " << level;
        }

        spdlog::info("Level {}: {} vertices verified", level, gen_coords.size());
    }
}

INSTANTIATE_TEST_SUITE_P(AllModesAndFormats, ContourTest,
    ::testing::Values(
        ContourParams{FitsImages() / "500x500.fits", CARTA::SmoothingMode::NoSmoothing, ContourData() / "500x500_contours"}
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
