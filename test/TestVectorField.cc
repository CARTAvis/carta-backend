/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "DataStream/Smoothing.h"
#include "Frame/Frame.h"
#include "Session/Session.h"

static const std::string IMAGE_SHAPE = "110 110 25 4";
static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class VectorFieldTest : public ::testing::Test {
public:
    class TestFrame : public Frame {
    public:
        TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
            : Frame(session_id, loader, hdu, default_z) {}

        static bool TestBlockSmooth(std::string sample_file_path, string stokes_type, int mip) {
            // Open the file
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));

            // Get stokes image data
            int stokes;
            if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
                return false;
            }

            casacore::Slicer section = frame->GetImageSlicer(AxisRange(frame->_z_index), stokes);
            std::vector<float> stokes_image_data;
            if (!frame->GetSlicerData(section, stokes_image_data)) {
                return false;
            }

            // Block averaging
            const int x = 0;
            const int y = 0;
            const int req_height = frame->_width - y;
            const int req_width = frame->_height - x;
            size_t num_region_rows = std::ceil((float)req_height / mip);
            size_t num_region_columns = std::ceil((float)req_width / mip);
            std::vector<float> down_sampled_data(num_region_rows * num_region_columns);

            // Original image data size
            const int num_image_columns = frame->_width;
            const int num_image_rows = frame->_height;

            BlockSmooth(stokes_image_data.data(), down_sampled_data.data(), num_image_columns, num_image_rows, num_region_columns,
                num_region_rows, x, y, mip);

            CheckDownSampledData(
                stokes_image_data, down_sampled_data, num_image_columns, num_image_rows, num_region_columns, num_region_rows, mip);

            return true;
        }

        static bool TestCalculation(std::string sample_file_path, int mip, double q_err = 0, double u_err = 0, double threshold = 0) {
            // Open the file
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));

            // Get Stokes I, Q, and U images data
            int stokes_i, stokes_q, stokes_u;
            if (!frame->GetStokesTypeIndex("Ix", stokes_i) || !frame->GetStokesTypeIndex("Qx", stokes_q) ||
                !frame->GetStokesTypeIndex("Ux", stokes_u)) {
                return false;
            }

            casacore::Slicer section_i = frame->GetImageSlicer(AxisRange(frame->_z_index), stokes_i);
            casacore::Slicer section_q = frame->GetImageSlicer(AxisRange(frame->_z_index), stokes_q);
            casacore::Slicer section_u = frame->GetImageSlicer(AxisRange(frame->_z_index), stokes_u);

            std::vector<float> stokes_i_data;
            std::vector<float> stokes_q_data;
            std::vector<float> stokes_u_data;

            if (!frame->GetSlicerData(section_i, stokes_i_data) || !frame->GetSlicerData(section_q, stokes_q_data) ||
                !frame->GetSlicerData(section_u, stokes_u_data)) {
                return false;
            }

            EXPECT_GT(stokes_i_data.size(), 0);
            EXPECT_EQ(stokes_i_data.size(), stokes_q_data.size());
            EXPECT_EQ(stokes_i_data.size(), stokes_u_data.size());

            // Block averaging
            // Calculate down sampled data size
            const int x = 0;
            const int y = 0;
            const int req_height = frame->_width - y;
            const int req_width = frame->_height - x;
            size_t num_region_rows = std::ceil((float)req_height / mip);
            size_t num_region_columns = std::ceil((float)req_width / mip);
            size_t res_size = num_region_rows * num_region_columns;

            std::vector<float> down_sampled_i(res_size);
            std::vector<float> down_sampled_q(res_size);
            std::vector<float> down_sampled_u(res_size);

            // Original image data size
            const int num_image_columns = frame->_width;
            const int num_image_rows = frame->_height;

            BlockSmooth(stokes_i_data.data(), down_sampled_i.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);
            BlockSmooth(stokes_q_data.data(), down_sampled_q.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);
            BlockSmooth(stokes_u_data.data(), down_sampled_u.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);

            // Calculate PI, FPI, and PA
            auto calc_pi = [&](float q, float u) {
                if (!std::isnan(q) && !isnan(u)) {
                    float result = sqrt(pow(q, 2) + pow(u, 2) - (pow(q_err, 2) + pow(u_err, 2)) / 2.0);
                    if (result > threshold) {
                        return result;
                    }
                }
                return std::numeric_limits<float>::quiet_NaN();
            };

            auto calc_tmp_pi = [&](float q, float u) {
                if (!std::isnan(q) && !isnan(u)) {
                    return sqrt(pow(q, 2) + pow(u, 2) - (pow(q_err, 2) + pow(u_err, 2)) / 2.0);
                }
                return std::numeric_limits<double>::quiet_NaN();
            };

            auto calc_fpi = [&](float i, float pi) {
                if (!std::isnan(i) && !isnan(pi)) {
                    float result = (pi / i);
                    if (result > threshold) {
                        return result;
                    }
                }
                return std::numeric_limits<float>::quiet_NaN();
            };

            auto calc_pa = [&](float q, float u) {
                if (!std::isnan(q) && !isnan(u)) {
                    return atan2(u, q) / 2;
                }
                return std::numeric_limits<float>::quiet_NaN();
            };

            // Calculate PI
            std::vector<float> pi(res_size);
            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pi.begin(), calc_pi);

            // Calculate FPI
            std::vector<float> tmp_pi(res_size);
            std::vector<float> fpi(res_size);
            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), tmp_pi.begin(), calc_tmp_pi);
            std::transform(down_sampled_i.begin(), down_sampled_i.end(), tmp_pi.begin(), fpi.begin(), calc_fpi);

            // Calculate PA
            std::vector<float> pa(res_size);
            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pa.begin(), calc_pa);

            // Check calculation results
            for (int i = 0; i < res_size; ++i) {
                float expected_pi = sqrt(pow(down_sampled_q[i], 2) + pow(down_sampled_u[i], 2) - (pow(q_err, 2) + pow(u_err, 2)) / 2.0);
                float expected_fpi = expected_pi / down_sampled_i[i];
                float expected_pa = atan2(down_sampled_u[i], down_sampled_q[i]) / 2; // i.e., 0.5 * tan^-1 (U∕Q)

                expected_pi = (expected_pi > threshold) ? expected_pi : std::numeric_limits<float>::quiet_NaN();
                expected_fpi = (expected_fpi > threshold) ? expected_fpi : std::numeric_limits<float>::quiet_NaN();

                if (!std::isnan(pi[i]) || !std::isnan(expected_pi)) {
                    EXPECT_FLOAT_EQ(pi[i], expected_pi);
                }
                if (!std::isnan(fpi[i]) || !std::isnan(expected_fpi)) {
                    EXPECT_FLOAT_EQ(fpi[i], expected_fpi);
                }
                if (!std::isnan(pa[i]) || !std::isnan(expected_pa)) {
                    EXPECT_FLOAT_EQ(pa[i], expected_pa);
                }
            }
            return true;
        }
    };

    static void CheckDownSampledData(const std::vector<float>& src_data, const std::vector<float>& dest_data, int src_width, int src_height,
        int dest_width, int dest_height, int mip) {
        EXPECT_GE(src_data.size(), 0);
        EXPECT_GE(dest_data.size(), 0);
        if ((src_width % mip == 0) && (src_height % mip == 0)) {
            EXPECT_TRUE(src_data.size() == dest_data.size() * pow(mip, 2));
        } else {
            EXPECT_TRUE(src_data.size() < dest_data.size() * pow(mip, 2));
        }

        for (int x = 0; x < dest_width; ++x) {
            for (int y = 0; y < dest_height; ++y) {
                int i_max = std::min(x * mip + mip, src_width);
                int j_max = std::min(y * mip + mip, src_height);
                float avg = 0;
                int count = 0;
                for (int i = x * mip; i < i_max; ++i) {
                    for (int j = y * mip; j < j_max; ++j) {
                        if (!std::isnan(src_data[j * src_width + i])) {
                            avg += src_data[j * src_width + i];
                            ++count;
                        }
                    }
                }
                avg /= count;
                if (count != 0) {
                    EXPECT_NEAR(dest_data[y * dest_width + x], avg, 1e-6);
                }
            }
        }
    }

    void TestMipLayerConversion(int mip, int image_width, int image_height) {
        int layer = Tile::MipToLayer(mip, image_width, image_height, TILE_SIZE, TILE_SIZE);
        EXPECT_EQ(mip, Tile::LayerToMip(layer, image_width, image_height, TILE_SIZE, TILE_SIZE));
    }

    static void TestRasterTilesGeneration(int image_width, int image_height, int mip) {
        std::vector<Tile> tiles;
        std::vector<CARTA::ImageBounds> image_bounds;
        int num_tile_rows, num_tile_columns;
        GenTilesAndBounds(image_width, image_height, mip, tiles, image_bounds, num_tile_rows, num_tile_columns);

        // Check the coverage of tiles on the image area
        std::vector<int> image_mask(image_width * image_height, 0);
        int count = 0;
        for (int i = 0; i < num_tile_columns; ++i) {
            for (int j = 0; j < num_tile_rows; ++j) {
                auto& bounds = image_bounds[j * num_tile_columns + i];
                for (int x = bounds.x_min(); x < bounds.x_max(); ++x) {
                    for (int y = bounds.y_min(); y < bounds.y_max(); ++y) {
                        image_mask[y * image_width + x] = 1;
                        count++;
                    }
                }
            }
        }

        for (int i = 0; i < image_mask.size(); ++i) {
            EXPECT_EQ(image_mask[i], 1);
        }
        EXPECT_EQ(count, image_mask.size());
    }

    static void GenTilesAndBounds(int image_width, int image_height, int mip, std::vector<Tile>& tiles,
        std::vector<CARTA::ImageBounds>& image_bounds, int& num_tile_rows, int& num_tile_columns) {
        // Generate tiles
        num_tile_columns = ceil((double)image_width / mip);
        num_tile_rows = ceil((double)image_height / mip);
        int num_tiles = num_tile_columns * num_tile_rows;
        int32_t tile_layer = Tile::MipToLayer(mip, image_width, image_height, TILE_SIZE, TILE_SIZE);

        tiles.resize(num_tile_rows * num_tile_columns);
        for (int i = 0; i < num_tile_columns; ++i) {
            for (int j = 0; j < num_tile_rows; ++j) {
                tiles[j * num_tile_columns + i].x = i;
                tiles[j * num_tile_columns + i].y = j;
                tiles[j * num_tile_columns + i].layer = tile_layer;
            }
        }

        // Generate image bounds with respect to tiles
        int tile_size_original = TILE_SIZE * mip;
        image_bounds.resize(num_tile_rows * num_tile_columns);
        for (int i = 0; i < num_tile_columns; ++i) {
            for (int j = 0; j < num_tile_rows; ++j) {
                auto& tile = tiles[j * num_tile_columns + i];
                auto& bounds = image_bounds[j * num_tile_columns + i];
                bounds.set_x_min(std::min(std::max(0, tile.x * tile_size_original), image_width));
                bounds.set_x_max(std::min(image_width, (tile.x + 1) * tile_size_original));
                bounds.set_y_min(std::min(std::max(0, tile.y * tile_size_original), image_height));
                bounds.set_y_max(std::min(image_height, (tile.y + 1) * tile_size_original));
            }
        }
    }
};

TEST_F(VectorFieldTest, TestBlockSmooth) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ix", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Qx", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ux", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Vx", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ix", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Qx", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ux", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Vx", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ix", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Qx", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Ux", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, "Vx", 8));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ix", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Qx", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ux", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Vx", 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ix", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Qx", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ux", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Vx", 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ix", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Qx", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Ux", 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, "Vx", 8));
}

TEST_F(VectorFieldTest, TestCalculation) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 2));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 4));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 8));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 2, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 4, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 8, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 2, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 4, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 8, 1e-3, 1e-3, 0.1));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 2));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 4));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 8));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 2, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 4, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 8, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 2, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 4, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 8, 1e-3, 1e-3, 0.1));
}

TEST_F(VectorFieldTest, TestMipLayerConversion) {
    TestMipLayerConversion(1, 512, 1024);
    TestMipLayerConversion(2, 512, 1024);
    TestMipLayerConversion(4, 512, 1024);
    TestMipLayerConversion(8, 512, 1024);

    TestMipLayerConversion(1, 1024, 1024);
    TestMipLayerConversion(2, 1024, 1024);
    TestMipLayerConversion(4, 1024, 1024);
    TestMipLayerConversion(8, 1024, 1024);

    TestMipLayerConversion(1, 5241, 5224);
    TestMipLayerConversion(2, 5241, 5224);
    TestMipLayerConversion(4, 5241, 5224);
    TestMipLayerConversion(8, 5241, 5224);
    TestMipLayerConversion(16, 5241, 5224);
}

TEST_F(VectorFieldTest, TestRasterTilesGeneration) {
    TestRasterTilesGeneration(513, 513, 1);
    TestRasterTilesGeneration(513, 513, 2);
    TestRasterTilesGeneration(513, 513, 4);
    TestRasterTilesGeneration(513, 513, 8);
    TestRasterTilesGeneration(513, 513, 16);
}
