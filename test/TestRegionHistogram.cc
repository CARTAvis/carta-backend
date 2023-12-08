/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

class RegionHistogramTest : public ::testing::Test {
public:
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
        CARTA::RegionHistogramData& region_histogram, bool is_annotation = false) {
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
        auto histogram_req_message = Message::SetHistogramRequirements(file_id, region_id);
        std::vector<CARTA::HistogramConfig> histogram_configs = {
            histogram_req_message.histograms().begin(), histogram_req_message.histograms().end()};
        if (!region_handler.SetHistogramRequirements(region_id, file_id, frame, histogram_configs)) {
            return false;
        }

        // Get histogram
        return region_handler.FillRegionHistogramData(
            [&](CARTA::RegionHistogramData histogram_data) { region_histogram = histogram_data; }, region_id, file_id);
    }
};

TEST_F(RegionHistogramTest, TestFitsRegionHistogram) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
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
    auto image_data = reader.ReadRegion({0, 0, 0}, {4, 4, 1});
    double expected_mean = std::accumulate(image_data.begin(), image_data.end(), 0.0) / image_data.size();
    ASSERT_DOUBLE_EQ(histogram_data.histograms().mean(), expected_mean);
}

TEST_F(RegionHistogramTest, TestFitsAnnotationRegionHistogram) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::RegionHistogramData histogram_data;
    bool ok = RegionHistogram(image_path, endpoints, histogram_data, true);
    ASSERT_FALSE(ok);
}
