/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "Util/Stokes.h"

using namespace carta;

class RegionHistogramTest : public ::testing::Test {
public:
    static CARTA::SetHistogramRequirements SetHistogramRequirements(int32_t file_id, int32_t region_id, const std::string& coordinate = "z",
        int32_t channel = CURRENT_Z, int32_t num_bins = AUTO_BIN_SIZE) {
        CARTA::SetHistogramRequirements set_histogram_requirements;
        set_histogram_requirements.set_file_id(file_id);
        set_histogram_requirements.set_region_id(region_id);
        auto* histograms = set_histogram_requirements.add_histograms();
        histograms->set_coordinate(coordinate);
        histograms->set_channel(channel);
        histograms->set_num_bins(num_bins);
        return set_histogram_requirements;
    }

    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys, bool is_annotation) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState for line region and set region (region_id updated)
        auto npoints(control_points.size());
        CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
        if (is_annotation) {
            region_type = CARTA::RegionType::ANNPOLYGON;
        }
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool RegionHistogram(const std::string& image_path, const std::vector<float>& endpoints,
        CARTA::RegionHistogramData& region_histogram, const std::string& coordinate = "z", bool is_annotation = false) {
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set polygon region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation)) {
            return false;
        }

        // Set histogram requirements
        auto histogram_req_message = SetHistogramRequirements(file_id, region_id, coordinate);
        std::vector<CARTA::HistogramConfig> histogram_configs = {
            histogram_req_message.histograms().begin(), histogram_req_message.histograms().end()};
        if (!region_handler.SetHistogramRequirements(region_id, file_id, frame, histogram_configs)) {
            return false;
        }

        // Get histogram
        return region_handler.FillRegionHistogramData(
            [&](CARTA::RegionHistogramData histogram_data) { region_histogram = histogram_data; }, region_id, file_id);
    }

    static bool RegionHistogramMatched(const std::string& image_path0, const std::string& image_path1, const std::vector<float>& endpoints,
        CARTA::RegionHistogramData& region_histogram) {
        std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
        std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
        std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
        std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));
        carta::RegionHandler region_handler;

        // Set polygon region in image0
        int file_id(0), region_id(-1);
        auto csys = frame0->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, false)) {
            return false;
        }
        // Set histogram requirements for region in image1
        file_id = 1;
        auto histogram_req_message = SetHistogramRequirements(file_id, region_id);
        std::vector<CARTA::HistogramConfig> histogram_configs = {
            histogram_req_message.histograms().begin(), histogram_req_message.histograms().end()};
        if (!region_handler.SetHistogramRequirements(region_id, file_id, frame1, histogram_configs)) {
            return false;
        }
        // Get histogram
        return region_handler.FillRegionHistogramData(
            [&](CARTA::RegionHistogramData histogram_data) { region_histogram = histogram_data; }, region_id, file_id);
    }
};

TEST_F(RegionHistogramTest, TestFitsRegionHistogram) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {1.0, 1.0, 1.0, 4.0, 4.0, 4.0, 4.0, 1.0};
    CARTA::RegionHistogramData histogram_data;
    bool ok = RegionHistogram(image_path, endpoints, histogram_data);

    // Check histogram data fields
    ASSERT_TRUE(ok);
    ASSERT_EQ(histogram_data.file_id(), 0);
    ASSERT_EQ(histogram_data.region_id(), 1);
    ASSERT_EQ(histogram_data.channel(), 0);
    ASSERT_EQ(histogram_data.stokes(), 0);
    ASSERT_TRUE(histogram_data.has_histograms());
    ASSERT_EQ(histogram_data.progress(), 1.0);
    ASSERT_TRUE(histogram_data.has_config());
    int expected_num_bins = sqrt(4 * 4); // region bounding box is 4x4
    ASSERT_EQ(histogram_data.histograms().num_bins(), expected_num_bins);

    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({1, 1, 0}, {5, 5, 1});
    double expected_mean = std::accumulate(image_data.begin(), image_data.end(), 0.0) / image_data.size();
    ASSERT_DOUBLE_EQ(histogram_data.histograms().mean(), expected_mean);
}

TEST_F(RegionHistogramTest, TestFitsAnnotationRegionHistogram) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::RegionHistogramData histogram_data;
    bool ok = RegionHistogram(image_path, endpoints, histogram_data, "z", true);
    ASSERT_FALSE(ok);
}

TEST_F(RegionHistogramTest, TestStokesRegionHistogram) {
    std::string image_path = FileFinder::FitsImagePath("noise_4d.fits"); // Stokes I and Q
    std::vector<float> endpoints = {1.0, 1.0, 1.0, 4.0, 4.0, 4.0, 4.0, 1.0};
    std::vector<std::string> coordinates{"z", "Iz", "Qz", "Uz", "Plinearz", "PFlinearz", "Panglez"};
    std::unordered_map<std::string, int> expected_stokes{{"z", 0}, {"Iz", 0}, {"Qz", 1}};
    int expected_num_bins = sqrt(4 * 4); // for 4x4 region

    for (auto& coordinate : coordinates) {
        CARTA::RegionHistogramData histogram_data;
        bool ok = RegionHistogram(image_path, endpoints, histogram_data, coordinate);

        if (expected_stokes.find(coordinate) != expected_stokes.end()) {
            ASSERT_TRUE(ok);
            ASSERT_TRUE(histogram_data.has_histograms());
            ASSERT_EQ(histogram_data.stokes(), expected_stokes[coordinate]);
            ASSERT_EQ(histogram_data.histograms().num_bins(), sqrt(4 * 4)); // for 4x4 region
            ASSERT_GT(histogram_data.histograms().bin_width(), 0.0);
            ASSERT_NE(histogram_data.histograms().first_bin_center(), 0.0);
            ASSERT_FALSE(std::isnan(histogram_data.histograms().mean()));
            ASSERT_FALSE(std::isnan(histogram_data.histograms().std_dev()));
        } else {
            // Histogram fails for U and all computed Stokes
            ASSERT_FALSE(ok);
            ASSERT_FALSE(histogram_data.has_histograms());
        }
    }
}

TEST_F(RegionHistogramTest, TestMatchedRegionHistogram) {
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::vector<float> endpoints = {1.0, 1.0, 1.0, 4.0, 4.0, 4.0, 4.0, 1.0};
    CARTA::RegionHistogramData histogram_data;
    bool ok = RegionHistogramMatched(image_path0, image_path1, endpoints, histogram_data);
    ASSERT_TRUE(ok);
    ASSERT_EQ(histogram_data.file_id(), 1);
    ASSERT_EQ(histogram_data.region_id(), 1);
    ASSERT_EQ(histogram_data.channel(), 0);
    ASSERT_EQ(histogram_data.stokes(), 0);
    ASSERT_TRUE(histogram_data.has_histograms());
    ASSERT_EQ(histogram_data.progress(), 1.0);
    ASSERT_TRUE(histogram_data.has_config());
    int expected_num_bins = sqrt(4 * 4); // region bounding box is 4x4
    ASSERT_EQ(histogram_data.histograms().num_bins(), expected_num_bins);
}
