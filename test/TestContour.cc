/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class ContourTest : public ::testing::Test {
public:
    void GenerateContour(
        int width, int height, std::string image_opts, const CARTA::FileType& file_type, const CARTA::SmoothingMode& smoothing_mode) {
        std::string image_shape = std::to_string(width) + " " + std::to_string(height);
        std::string file_path;
        if (file_type == CARTA::FileType::HDF5) {
            file_path = ImageGenerator::GeneratedHdf5ImagePath(image_shape, image_opts);
        } else {
            file_path = ImageGenerator::GeneratedFitsImagePath(image_shape, image_opts);
        }

        std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(file_path));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

        spdlog::info("The generated image contains random pixels values with mean = 0 and STD = 1.");
        std::vector<double> levels{0, -1, 1}; // Contour levels
        auto set_contour_params = Message::SetContourParameters(0, 0, 0, width, 0, height, levels, smoothing_mode, 4, 4, 8, 100000);

        EXPECT_TRUE(frame->SetContourParameters(set_contour_params));

        std::unordered_map<double, double> progresses;
        std::unordered_map<double, std::vector<float>> vertices_map;
        // Initialize vertices map with requested contour levels
        for (auto level : levels) {
            vertices_map[level] = {};
        }

        auto callback = [&](double level, double progress, const std::vector<float>& vertices, const std::vector<int>& indices) {
            std::unique_lock<std::mutex> ulock(_callback_mutex);
            if (vertices_map.count(level)) {
                vertices_map[level].insert(vertices_map[level].end(), vertices.begin(), vertices.end());
            }
            progresses[level] = progress;
            ulock.unlock();
        };
        EXPECT_TRUE(frame->ContourImage(callback));

        // Check the number of resulting contour levels
        EXPECT_EQ(progresses.size(), levels.size());

        // Check are contour progresses completely for all contour levels
        for (auto progress : progresses) {
            EXPECT_DOUBLE_EQ(progress.second, 1);
        }

        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        for (auto vertices_level : vertices_map) {
            // Fill in vertices coordinates
            std::vector<std::pair<double, double>> coords;
            for (int i = 0; i < vertices_level.second.size() / 2; ++i) {
                double x = vertices_level.second[i * 2];
                double y = vertices_level.second[i * 2 + 1];
                coords.emplace_back(std::make_pair(x, y));
            }

            // Check are they real vertices
            int count(0);
            for (auto coord : coords) {
                if (smoothing_mode == CARTA::SmoothingMode::NoSmoothing) {
                    // Only verify vertices coordinate which are calculated from raw pixels, i.e., with no smoothing mode
                    EXPECT_TRUE(IsVertex(reader, coord.first, coord.second, vertices_level.first, width, height));
                }
                ++count;
            }
            spdlog::info("For contour level {}, number of vertices is {}", vertices_level.first, count);
        }
    }

    bool IsVertex(const std::shared_ptr<DataReader>& reader, double x, double y, double level, int width, int height) {
        // Shift to pixel coordinate
        x -= 0.5;
        y -= 0.5;
        int pt_x = (int)floor(x);
        int pt_y = (int)floor(y);

        if (!InImage(pt_x, pt_y, width, height)) {
            return false;
        }

        double pt1_pix = (double)(reader->ReadPointXY(pt_x, pt_y));
        pt1_pix = std::isnan(pt1_pix) ? -std::numeric_limits<float>::max() : pt1_pix;

        auto is_vertex = [&](int x, int y) {
            if (InImage(x, y, width, height)) {
                double pt2_pix = (double)(reader->ReadPointXY(x, y));
                pt2_pix = std::isnan(pt2_pix) ? -std::numeric_limits<float>::max() : pt2_pix;
                if ((pt1_pix <= level && level <= pt2_pix) || (pt2_pix <= level && level <= pt1_pix)) {
                    return true;
                }
            }
            return false;
        };

        return (is_vertex(pt_x - 1, pt_y - 1) || is_vertex(pt_x, pt_y - 1) || is_vertex(pt_x - 1, pt_y) || is_vertex(pt_x + 1, pt_y + 1) ||
                is_vertex(pt_x, pt_y + 1) || is_vertex(pt_x + 1, pt_y) || is_vertex(pt_x - 1, pt_y + 1) || is_vertex(pt_x + 1, pt_y - 1));
    }

    bool InImage(int x, int y, int width, int height) {
        return (0 <= x && x < width && 0 <= y && y < height);
    }

private:
    std::mutex _callback_mutex;
};

TEST_F(ContourTest, NoSmoothingFitsFile) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::FITS, CARTA::SmoothingMode::NoSmoothing);
}
TEST_F(ContourTest, NoSmoothingFitsFileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS_NAN, CARTA::FileType::FITS, CARTA::SmoothingMode::NoSmoothing);
}

TEST_F(ContourTest, GaussianBlurFitsFile) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::FITS, CARTA::SmoothingMode::GaussianBlur);
}
TEST_F(ContourTest, GaussianBlurFitsFileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS_NAN, CARTA::FileType::FITS, CARTA::SmoothingMode::GaussianBlur);
}

TEST_F(ContourTest, BlockAverageFitsFile) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::FITS, CARTA::SmoothingMode::BlockAverage);
}
TEST_F(ContourTest, BlockAverageFitsFileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS_NAN, CARTA::FileType::FITS, CARTA::SmoothingMode::BlockAverage);
}

TEST_F(ContourTest, NoSmoothingHdf5File) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::HDF5, CARTA::SmoothingMode::NoSmoothing);
}
TEST_F(ContourTest, NoSmoothingHdf5FileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS_NAN, CARTA::FileType::HDF5, CARTA::SmoothingMode::NoSmoothing);
}

TEST_F(ContourTest, GaussianBlurHdf5File) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::HDF5, CARTA::SmoothingMode::GaussianBlur);
}
TEST_F(ContourTest, GaussianBlurHdf5FileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS_NAN, CARTA::FileType::HDF5, CARTA::SmoothingMode::GaussianBlur);
}

TEST_F(ContourTest, BlockAverageHdf5File) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::HDF5, CARTA::SmoothingMode::BlockAverage);
}
TEST_F(ContourTest, BlockAverageHdf5FileNaN) {
    GenerateContour(500, 500, IMAGE_OPTS, CARTA::FileType::HDF5, CARTA::SmoothingMode::BlockAverage);
}
