/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <tuple>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

class ContourTest : public ::testing::Test {
public:
    // This test verifies the correctness of contour generation in CARTA for both
    // FITS and HDF5 image files. It loads the target image into a Frame, applies
    // contour parameters (multiple levels and a specified smoothing mode), and
    // generates contours over the full image extent. The test asserts that:
    //
    //  1. Contours are generated for all requested levels.
    //  2. Each contour level reports progress reaching 100%.
    //  3. When no smoothing is applied, the generated contour vertices correspond
    //     to valid pixel-derived values in the underlying dataset.
    // 
    // The test also logs the number of vertices produced per contour level to
    // confirm completeness of the contouring process.
    void GenerateContour(std::string filename, const CARTA::FileType& file_type, const CARTA::SmoothingMode& smoothing_mode) {
        std::string file_path;

        if (file_type == CARTA::FileType::HDF5) {
            file_path = (TestRoot() / "data" / "images" / "hdf5" / filename);
        } else {
            file_path = (TestRoot() / "data" / "images" / "fits" / filename);
        }

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

        spdlog::info("The generated image contains random pixels values with mean = 0 and STD = 1.");
        std::vector<double> levels{0, -1, 1}; // Contour levels
        auto set_contour_params =
            Message::SetContourParameters(0, 0, 0, frame->Width(), 0, frame->Height(), levels, smoothing_mode, 4, 4, 8, 100000);

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

        EXPECT_TRUE(frame->ContourImage(callback, frame->CurrentZ()));

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
                    EXPECT_TRUE(IsVertex(reader, coord.first, coord.second, vertices_level.first, frame->Width(), frame->Height()));
                }
                ++count;
            }
            spdlog::info("For contour level {}, number of vertices is {}", vertices_level.first, count);
        }
    }

    // This helper function checks whether a given (x, y) coordinate corresponds
    // to a valid contour vertex at a specified contour level. It does so by:
    //
    //  1. Converting the floating-point coordinates into pixel indices.
    //  2. Verifying that the central pixel lies within the image bounds.
    //  3. Reading the pixel value at the central location and treating NaN values
    //     as a large negative sentinel.
    //  4. Comparing the central pixel value against each of its 8 neighboring
    //     pixels to determine if the specified contour level lies between them.
    //
    // The function returns true if the contour level crosses between the central
    // pixel value and any of its neighbors, meaning the coordinate is part of a
    // valid contour line. Otherwise, it returns false.
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

class ContourTestParameterized : public ContourTest, public ::testing::WithParamInterface<std::tuple<std::string, CARTA::SmoothingMode>> {};

TEST_P(ContourTestParameterized, Generate) {
    auto [filename, smoothing_mode] = GetParam();
    GenerateContour(filename, CARTA::FileType::FITS, smoothing_mode);
}

/*
    The parameterised contour tests verify that GenerateContour correctly produces contour
    vertices for a given FITS image file and smoothing mode.

    For each (filename, smoothing_mode) pair, the test checks that all requested contour
    levels are generated, that contour progress reaches 100%, and (when no smoothing is
    applied) that each vertex lies on a real contour in the original image data.
*/
INSTANTIATE_TEST_SUITE_P(FitsFiles, ContourTestParameterized,
    ::testing::Values(
        // Contours at all requested levels are generated; progress for each level reaches 100%; all vertices match actual contour positions
        // in raw image data
        std::make_tuple("500_500_image_opts.fits", CARTA::SmoothingMode::NoSmoothing),
        // Same as above, but with NaN pixel values present; NaNs are ignored and do not break contour generation
        std::make_tuple("500_500_image_opts_nan.fits", CARTA::SmoothingMode::NoSmoothing),
        // Contours are generated and complete; vertex correctness check is skipped for smoothed data
        std::make_tuple("500_500_image_opts.fits", CARTA::SmoothingMode::GaussianBlur),
        // Same as above, but with NaNs present; contour generation remains correct and complete
        std::make_tuple("500_500_image_opts_nan.fits", CARTA::SmoothingMode::GaussianBlur),
        // Contours are generated and complete; vertex correctness check is skipped for smoothed data
        std::make_tuple("500_500_image_opts.fits", CARTA::SmoothingMode::BlockAverage),
        // Same as above, but with NaNs present; contour generation remains correct and complete
        std::make_tuple("500_500_image_opts_nan.fits", CARTA::SmoothingMode::BlockAverage)));
