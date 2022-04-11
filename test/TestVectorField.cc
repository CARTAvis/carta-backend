/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "BackendModel.h"
#include "CommonTestUtilities.h"
#include "DataStream/Compression.h"
#include "DataStream/Smoothing.h"
#include "Frame/Frame.h"
#include "Session/Session.h"

static const std::string IMAGE_SHAPE = "1110 1110 25 4";
static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    FRIEND_TEST(VectorFieldTest, ExampleFriendTest);

    std::vector<int> GetLoaderMips() {
        std::vector<int> results;
        for (int loader_mip = 0; loader_mip < 17; ++loader_mip) {
            if (_loader->HasMip(loader_mip)) {
                results.push_back(loader_mip);
            }
        }
        return results;
    }

    bool GetLoaderDownSampledData(std::vector<float>& down_sampled_data, int channel, int stokes, CARTA::ImageBounds& bounds, int mip) {
        if (!ImageBoundsValid(bounds)) {
            return false;
        }
        if (!_loader->HasMip(mip) || !_loader->GetDownsampledRasterData(down_sampled_data, channel, stokes, bounds, mip, _image_mutex)) {
            return false;
        }
        return true;
    }

    bool GetDownSampledData(std::vector<float>& down_sampled_data, int& down_sampled_width, int& down_sampled_height, int channel,
        int stokes, CARTA::ImageBounds& bounds, int mip) {
        if (!ImageBoundsValid(bounds)) {
            return false;
        }

        // Get original raster tile data
        int x_min = bounds.x_min();
        int x_max = bounds.x_max() - 1;
        int y_min = bounds.y_min();
        int y_max = bounds.y_max() - 1;

        casacore::Slicer tile_section = GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes);
        std::vector<float> tile_data(tile_section.length().product());
        if (!GetSlicerData(tile_section, tile_data.data())) {
            return false;
        }

        int tile_original_width = bounds.x_max() - bounds.x_min();
        int tile_original_height = bounds.y_max() - bounds.y_min();
        down_sampled_width = std::ceil((float)tile_original_width / mip);
        down_sampled_height = std::ceil((float)tile_original_height / mip);

        // Get down sampled raster tile data by block averaging
        down_sampled_data.resize(down_sampled_height * down_sampled_width);
        return BlockSmooth(tile_data.data(), down_sampled_data.data(), tile_original_width, tile_original_height, down_sampled_width,
            down_sampled_height, 0, 0, mip);
    }

    bool ImageBoundsValid(const CARTA::ImageBounds& bounds) {
        int tile_original_width = bounds.x_max() - bounds.x_min();
        int tile_original_height = bounds.y_max() - bounds.y_min();
        if (tile_original_width * tile_original_height <= 0) {
            return false;
        }
        return true;
    }
};

class VectorFieldTest : public ::testing::Test {
public:
    bool TestLoaderDownSampledData(
        std::string image_shape, std::string image_opts, std::string stokes_type, std::vector<int>& loader_mips) {
        // Create the sample image
        std::string file_path_string = ImageGenerator::GeneratedHdf5ImagePath(image_shape, image_opts);

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(file_path_string), "0"));

        // Get loader mips
        loader_mips = frame->GetLoaderMips();
        EXPECT_TRUE(!loader_mips.empty());

        // Get Stokes index
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        int channel = frame->CurrentZ();

        int down_sampled_width;
        int down_sampled_height;
        CARTA::ImageBounds bounds;
        bounds.set_x_min(0);
        bounds.set_x_max(frame->Width());
        bounds.set_y_min(0);
        bounds.set_y_max(frame->Height());

        for (auto loader_mip : loader_mips) {
            // Get (HDF5) loader downsampled data
            std::vector<float> down_sampled_data1;
            if (!frame->GetLoaderDownSampledData(down_sampled_data1, channel, stokes, bounds, loader_mip)) {
                return false;
            }

            // Get downsampled data from the full resolution raster data
            std::vector<float> down_sampled_data2;
            if (!frame->GetDownSampledData(
                    down_sampled_data2, down_sampled_width, down_sampled_height, channel, stokes, bounds, loader_mip)) {
                return false;
            }

            // Compare two downsampled data
            CmpVectors(down_sampled_data1, down_sampled_data2, 1e-6);
        }
        return true;
    }

    bool TestBlockSmoothDownSampledData(
        std::string image_shape, std::string image_opts, std::string stokes_type, int mip, int loader_mip, float abs_error) {
        // Create the sample image
        std::string file_path_string = ImageGenerator::GeneratedHdf5ImagePath(image_shape, image_opts);

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(file_path_string), "0"));

        // Get Stokes index
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        int channel = frame->CurrentZ();
        int image_width = frame->Width();
        int image_height = frame->Height();

        CARTA::ImageBounds bounds;
        bounds.set_x_min(0);
        bounds.set_x_max(image_width);
        bounds.set_y_min(0);
        bounds.set_y_max(image_height);

        // Get (HDF5) loader downsampled data
        std::vector<float> loader_down_sampled_data;
        if (!frame->GetLoaderDownSampledData(loader_down_sampled_data, channel, stokes, bounds, loader_mip)) {
            return false;
        }

        // Get down sampled data from the smaller loader down sampled data
        int down_sampled_width_1st = std::ceil((float)image_width / loader_mip);
        int down_sampled_height_1st = std::ceil((float)image_height / loader_mip);
        int mip_2nd = mip / loader_mip;
        int down_sampled_width_2nd = std::ceil((float)down_sampled_width_1st / mip_2nd);
        int down_sampled_height_2nd = std::ceil((float)down_sampled_height_1st / mip_2nd);
        std::vector<float> down_sampled_data1(down_sampled_height_2nd * down_sampled_width_2nd);
        if (!BlockSmooth(loader_down_sampled_data.data(), down_sampled_data1.data(), down_sampled_width_1st, down_sampled_height_1st,
                down_sampled_width_2nd, down_sampled_height_2nd, 0, 0, mip_2nd)) {
            return false;
        }

        // Check does the function BlockSmooth work well
        CheckDownSampledData(loader_down_sampled_data, down_sampled_data1, down_sampled_width_1st, down_sampled_height_1st,
            down_sampled_width_2nd, down_sampled_height_2nd, mip_2nd);

        // Get down sampled data from the full resolution raster data
        int down_sampled_width;
        int down_sampled_height;
        std::vector<float> down_sampled_data2;
        if (!frame->GetDownSampledData(down_sampled_data2, down_sampled_width, down_sampled_height, channel, stokes, bounds, mip)) {
            return false;
        }
        EXPECT_EQ(down_sampled_width, down_sampled_width_2nd);
        EXPECT_EQ(down_sampled_height, down_sampled_height_2nd);

        if (image_width % loader_mip != 0) {
            // Remove the right edge pixels
            for (int i = 0; i < down_sampled_data2.size(); ++i) {
                if ((i + 1) % down_sampled_width == 0) {
                    down_sampled_data1[i] = down_sampled_data2[i] = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }

        if (image_height % loader_mip != 0) {
            // Remove the bottom edge pixels
            for (int i = 0; i < down_sampled_data2.size(); ++i) {
                if (i / down_sampled_width == down_sampled_height - 1) {
                    down_sampled_data1[i] = down_sampled_data2[i] = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }

        // Compare two down sampled data
        CmpVectors(down_sampled_data1, down_sampled_data2, abs_error);
        return true;
    }

    static bool TestTilesData(std::string image_opts, const CARTA::FileType& file_type, std::string stokes_type, int mip) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Get Stokes index
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        // Get tiles
        std::vector<Tile> tiles;
        int image_width = frame->Width();
        int image_height = frame->Height();
        GetTiles(image_width, image_height, mip, tiles);

        // Get full 2D stokes data
        int channel = frame->CurrentZ();
        casacore::Slicer section = frame->GetImageSlicer(AxisRange(channel), stokes);
        std::vector<float> image_data(section.length().product());
        if (!frame->GetSlicerData(section, image_data.data())) {
            return false;
        }
        EXPECT_EQ(image_data.size(), image_width * image_height);

        // Check tiles data
        int count = 0;
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile = tiles[i];
            auto bounds = GetImageBounds(tile, image_width, image_height, mip);

            // Get the tile data
            int tile_original_width = bounds.x_max() - bounds.x_min();
            int tile_original_height = bounds.y_max() - bounds.y_min();
            if (tile_original_width * tile_original_height == 0) { // Don't get the tile data with zero area
                continue;
            }

            int x_min = bounds.x_min();
            int x_max = bounds.x_max() - 1;
            int y_min = bounds.y_min();
            int y_max = bounds.y_max() - 1;
            casacore::Slicer tile_section =
                frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes);

            std::vector<float> tile_data(tile_section.length().product());
            if (!frame->GetSlicerData(tile_section, tile_data.data())) {
                return false;
            }

            EXPECT_GT(tile_data.size(), 0);
            EXPECT_EQ(tile_data.size(), tile_original_width * tile_original_height);

            // Check is the tile coordinate correct when converted to the original image coordinate
            for (int j = 0; j < tile_data.size(); ++j) {
                // Convert the tile coordinate to image coordinate
                int tile_x = j % tile_original_width;
                int tile_y = j / tile_original_width;
                int image_x = x_min + tile_x;
                int image_y = y_min + tile_y;
                int image_index = image_y * image_width + image_x;
                if (!std::isnan(image_data[image_index]) || !std::isnan(tile_data[j])) {
                    EXPECT_FLOAT_EQ(image_data[image_index], tile_data[j]);
                }
                ++count;
            }
        }
        EXPECT_EQ(image_data.size(), count);
        return true;
    }

    static bool TestBlockSmooth(std::string image_opts, const CARTA::FileType& file_type, std::string stokes_type, int mip) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Get stokes image data
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        casacore::Slicer section = frame->GetImageSlicer(AxisRange(frame->CurrentZ()), stokes);
        std::vector<float> image_data(section.length().product());
        if (!frame->GetSlicerData(section, image_data.data())) {
            return false;
        }

        // Original image data size
        int image_width = frame->Width();
        int image_height = frame->Height();

        // Block averaging
        int down_sampled_height = std::ceil((float)image_height / mip);
        int down_sampled_width = std::ceil((float)image_width / mip);
        std::vector<float> down_sampled_data(down_sampled_height * down_sampled_width);

        BlockSmooth(
            image_data.data(), down_sampled_data.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);

        CheckDownSampledData(image_data, down_sampled_data, image_width, image_height, down_sampled_width, down_sampled_height, mip);

        return true;
    }

    static bool TestTileCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional, double q_error = 0,
        double u_error = 0, double threshold = 0) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Get Stokes I, Q, and U indices
        int stokes_i, stokes_q, stokes_u;
        if (!frame->GetStokesTypeIndex("Ix", stokes_i) || !frame->GetStokesTypeIndex("Qx", stokes_q) ||
            !frame->GetStokesTypeIndex("Ux", stokes_u)) {
            return false;
        }

        // Get current channel
        int channel = frame->CurrentZ();

        // Get tiles
        std::vector<Tile> tiles;
        int image_width = frame->Width();
        int image_height = frame->Height();
        GetTiles(image_width, image_height, mip, tiles);

        EXPECT_GT(tiles.size(), 0);

        // Set results data
        std::vector<CARTA::TileData> tiles_data_pi(tiles.size());
        std::vector<CARTA::TileData> tiles_data_pa(tiles.size());
        std::vector<std::vector<float>> pis(tiles.size());
        std::vector<std::vector<float>> pas(tiles.size());

        // Get tiles data
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile = tiles[i];
            auto bounds = GetImageBounds(tile, image_width, image_height, mip);

            // Don't get the tile data with zero area
            int tile_original_width = bounds.x_max() - bounds.x_min();
            int tile_original_height = bounds.y_max() - bounds.y_min();
            if (tile_original_width * tile_original_height == 0) {
                continue;
            }

            // Get raster tile data
            int x_min = bounds.x_min();
            int x_max = bounds.x_max() - 1;
            int y_min = bounds.y_min();
            int y_max = bounds.y_max() - 1;

            casacore::Slicer tile_section_i =
                frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_i);
            casacore::Slicer tile_section_q =
                frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_q);
            casacore::Slicer tile_section_u =
                frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_u);

            std::vector<float> tile_data_i(tile_section_i.length().product());
            std::vector<float> tile_data_q(tile_section_q.length().product());
            std::vector<float> tile_data_u(tile_section_u.length().product());

            if (!frame->GetSlicerData(tile_section_i, tile_data_i.data()) || !frame->GetSlicerData(tile_section_q, tile_data_q.data()) ||
                !frame->GetSlicerData(tile_section_u, tile_data_u.data())) {
                return false;
            }

            EXPECT_EQ(tile_data_i.size(), tile_original_width * tile_original_height);
            EXPECT_EQ(tile_data_q.size(), tile_original_width * tile_original_height);
            EXPECT_EQ(tile_data_u.size(), tile_original_width * tile_original_height);

            // Block averaging, get down sampled data
            int down_sampled_height = std::ceil((float)tile_original_height / mip);
            int down_sampled_width = std::ceil((float)tile_original_width / mip);
            int down_sampled_area = down_sampled_height * down_sampled_width;

            if (mip > 1) {
                EXPECT_GT(tile_original_width, down_sampled_width);
                EXPECT_GT(tile_original_height, down_sampled_height);
            } else {
                EXPECT_EQ(tile_original_width, down_sampled_width);
                EXPECT_EQ(tile_original_height, down_sampled_height);
            }

            std::vector<float> down_sampled_i(down_sampled_area);
            std::vector<float> down_sampled_q(down_sampled_area);
            std::vector<float> down_sampled_u(down_sampled_area);

            BlockSmooth(tile_data_i.data(), down_sampled_i.data(), tile_original_width, tile_original_height, down_sampled_width,
                down_sampled_height, 0, 0, mip);
            BlockSmooth(tile_data_q.data(), down_sampled_q.data(), tile_original_width, tile_original_height, down_sampled_width,
                down_sampled_height, 0, 0, mip);
            BlockSmooth(tile_data_u.data(), down_sampled_u.data(), tile_original_width, tile_original_height, down_sampled_width,
                down_sampled_height, 0, 0, mip);

            CheckDownSampledData(
                tile_data_i, down_sampled_i, tile_original_width, tile_original_height, down_sampled_width, down_sampled_height, mip);
            CheckDownSampledData(
                tile_data_q, down_sampled_q, tile_original_width, tile_original_height, down_sampled_width, down_sampled_height, mip);
            CheckDownSampledData(
                tile_data_u, down_sampled_u, tile_original_width, tile_original_height, down_sampled_width, down_sampled_height, mip);

            // Calculate PI, FPI, and PA
            auto calc_pi = [&](float q, float u) {
                if (!std::isnan(q) && !isnan(u)) {
                    float result = sqrt(pow(q, 2) + pow(u, 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0);
                    if (fractional) {
                        return result;
                    } else {
                        if (result > threshold) {
                            return result;
                        }
                    }
                }
                return std::numeric_limits<float>::quiet_NaN();
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

            auto reset_pa = [&](float pi, float pa) {
                if (std::isnan(pi)) {
                    return std::numeric_limits<float>::quiet_NaN();
                }
                return pa;
            };

            // Set PI/PA results
            auto& pi = pis[i];
            auto& pa = pas[i];
            pi.resize(down_sampled_area);
            pa.resize(down_sampled_area);

            // Calculate PI
            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pi.begin(), calc_pi);

            if (fractional) { // Calculate FPI
                std::transform(down_sampled_i.begin(), down_sampled_i.end(), pi.begin(), pi.begin(), calc_fpi);
            }

            // Calculate PA
            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pa.begin(), calc_pa);

            // Set NaN for PA if PI/FPI is NaN
            std::transform(pi.begin(), pi.end(), pa.begin(), pa.begin(), reset_pa);

            // Check calculation results
            for (int j = 0; j < down_sampled_area; ++j) {
                float expected_pi;
                if (fractional) {
                    expected_pi = sqrt(pow(down_sampled_q[j], 2) + pow(down_sampled_u[j], 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0) /
                                  down_sampled_i[j];
                } else {
                    expected_pi = sqrt(pow(down_sampled_q[j], 2) + pow(down_sampled_u[j], 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0);
                }

                float expected_pa = atan2(down_sampled_u[j], down_sampled_q[j]) / 2; // j.e., 0.5 * tan^-1 (U∕Q)

                expected_pi = (expected_pi > threshold) ? expected_pi : std::numeric_limits<float>::quiet_NaN();
                expected_pa = (expected_pi > threshold) ? expected_pa : std::numeric_limits<float>::quiet_NaN();

                if (!std::isnan(pi[j]) || !std::isnan(expected_pi)) {
                    EXPECT_FLOAT_EQ(pi[j], expected_pi);
                }
                if (!std::isnan(pa[j]) || !std::isnan(expected_pa)) {
                    EXPECT_FLOAT_EQ(pa[j], expected_pa);
                }
            }

            // Fill tiles protobuf data
            auto& tile_pi = tiles_data_pi[i];
            FillTileData(&tile_pi, tiles[i].x, tiles[i].y, tiles[i].layer, mip, down_sampled_width, down_sampled_height, pi,
                CARTA::CompressionType::NONE, 0);

            auto& tile_pa = tiles_data_pa[i];
            FillTileData(&tile_pa, tiles[i].x, tiles[i].y, tiles[i].layer, mip, down_sampled_width, down_sampled_height, pa,
                CARTA::CompressionType::NONE, 0);
        }

        // Check tiles protobuf data
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile_pi = tiles_data_pi[i];
            std::string buf_pi = tile_pi.image_data();
            std::vector<float> pi2(buf_pi.size() / sizeof(float));
            memcpy(pi2.data(), buf_pi.data(), buf_pi.size());

            auto& tile_pa = tiles_data_pa[i];
            std::string buf_pa = tile_pa.image_data();
            std::vector<float> pa2(buf_pa.size() / sizeof(float));
            memcpy(pa2.data(), buf_pa.data(), buf_pa.size());

            auto& pi = pis[i];
            auto& pa = pas[i];

            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        return true;
    }

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
        GetTiles(image_width, image_height, mip, tiles);

        // Check the coverage of tiles on the image area
        std::vector<int> image_mask(image_width * image_height, 0);
        int count = 0;
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile = tiles[i];
            auto bounds = GetImageBounds(tile, image_width, image_height, mip);
            for (int x = bounds.x_min(); x < bounds.x_max(); ++x) {
                for (int y = bounds.y_min(); y < bounds.y_max(); ++y) {
                    image_mask[y * image_width + x] = 1;
                    count++;
                }
            }
        }

        for (int i = 0; i < image_mask.size(); ++i) {
            EXPECT_EQ(image_mask[i], 1);
        }
        EXPECT_EQ(count, image_mask.size());
    }

    static bool TestVectorFieldCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 0,
        int stokes_angle = 0) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        // Get Stokes I, Q, and U indices
        int stokes_i, stokes_q, stokes_u;
        if (!frame->GetStokesTypeIndex("Ix", stokes_i) || !frame->GetStokesTypeIndex("Qx", stokes_q) ||
            !frame->GetStokesTypeIndex("Ux", stokes_u)) {
            return false;
        }

        int channel = frame->CurrentZ();
        int image_width = frame->Width();
        int image_height = frame->Height();

        // Get raster tile data
        int x_min = 0;
        int x_max = image_width - 1;
        int y_min = 0;
        int y_max = image_height - 1;

        casacore::Slicer stokes_section_i =
            frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_i);
        casacore::Slicer stokes_section_q =
            frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_q);
        casacore::Slicer stokes_section_u =
            frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_u);

        std::vector<float> stokes_data_i(stokes_section_i.length().product());
        std::vector<float> stokes_data_q(stokes_section_q.length().product());
        std::vector<float> stokes_data_u(stokes_section_u.length().product());

        if (!frame->GetSlicerData(stokes_section_i, stokes_data_i.data()) ||
            !frame->GetSlicerData(stokes_section_q, stokes_data_q.data()) ||
            !frame->GetSlicerData(stokes_section_u, stokes_data_u.data())) {
            return false;
        }

        // Block averaging, get down sampled data
        int down_sampled_width = std::ceil((float)image_width / mip);
        int down_sampled_height = std::ceil((float)image_height / mip);
        int down_sampled_area = down_sampled_height * down_sampled_width;

        std::vector<float> down_sampled_i(down_sampled_area);
        std::vector<float> down_sampled_q(down_sampled_area);
        std::vector<float> down_sampled_u(down_sampled_area);

        BlockSmooth(
            stokes_data_i.data(), down_sampled_i.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);
        BlockSmooth(
            stokes_data_q.data(), down_sampled_q.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);
        BlockSmooth(
            stokes_data_u.data(), down_sampled_u.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);

        // Reset Q and U errors as 0 if debiasing is not used
        if (!debiasing) {
            q_error = u_error = 0;
        }

        // Calculate PI, FPI, and PA
        auto calc_pi = [&](float q, float u) {
            if (!std::isnan(q) && !isnan(u)) {
                float result = sqrt(pow(q, 2) + pow(u, 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0);
                if (fractional) {
                    return result;
                } else {
                    if (result > threshold) {
                        return result;
                    }
                }
            }
            return std::numeric_limits<float>::quiet_NaN();
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

        auto reset_pa = [&](float pi, float pa) {
            if (std::isnan(pi)) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return pa;
        };

        // Set PI/PA results
        std::vector<float> pi(down_sampled_area);
        std::vector<float> pa(down_sampled_area);

        // Calculate PI
        std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pi.begin(), calc_pi);

        if (fractional) { // Calculate FPI
            std::transform(down_sampled_i.begin(), down_sampled_i.end(), pi.begin(), pi.begin(), calc_fpi);
        }

        // Calculate PA
        std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pa.begin(), calc_pa);

        // Set NaN for PA if PI/FPI is NaN
        std::transform(pi.begin(), pi.end(), pa.begin(), pa.begin(), reset_pa);

        // =======================================================================================================
        // Calculate the vector field tile by tile with the new Frame function

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        std::vector<float> pi2(down_sampled_area);
        std::vector<float> pa2(down_sampled_area);
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                // Fill PI values
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, down_sampled_width, pi2);
            }

            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                // Fill PA values
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, down_sampled_width, pa2);
            }

            // Record progress
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback);

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightAndBottomEdgeData(pi, pi2, pa, pa2, down_sampled_width, down_sampled_height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-5);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
        return true;
    }

    static void CheckProgresses(const std::vector<double>& progresses) {
        EXPECT_TRUE(!progresses.empty());
        if (!progresses.empty()) {
            EXPECT_EQ(progresses.back(), 1);
        }
    }

    static void GetTileData(const CARTA::TileData& tile, int down_sampled_width, std::vector<float>& array) {
        int tile_x = tile.x();
        int tile_y = tile.y();
        int tile_width = tile.width();
        int tile_height = tile.height();
        int tile_layer = tile.layer();
        std::string buf = tile.image_data();
        std::vector<float> val(buf.size() / sizeof(float));
        memcpy(val.data(), buf.data(), buf.size());

        for (int i = 0; i < val.size(); ++i) {
            int x = tile_x * TILE_SIZE + (i % tile_width);
            int y = tile_y * TILE_SIZE + (i / tile_width);
            array[y * down_sampled_width + x] = val[i];
        }
    }

    static void TestVectorFieldCalc2(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 0,
        int stokes_angle = 0) {
        // Create the sample image
        std::string file_path;
        if (file_type == CARTA::FileType::HDF5) {
            file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int down_sampled_width;
        int down_sampled_height;
        std::vector<float> pi;
        std::vector<float> pa;
        CalcPiAndPa(file_path, file_type, channel, mip, debiasing, fractional, threshold, q_error, u_error, down_sampled_width,
            down_sampled_height, pi, pa);

        // =======================================================================================================
        // Calculate the vector field tile by tile with the new Frame function

        // Open file with the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path), "0"));

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        std::vector<float> pi2(down_sampled_width * down_sampled_height);
        std::vector<float> pa2(down_sampled_width * down_sampled_height);
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                // Fill PI values
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, down_sampled_width, pi2);
            }

            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                // Fill PA values
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, down_sampled_width, pa2);
            }

            // Record progress
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback);

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightAndBottomEdgeData(pi, pi2, pa, pa2, down_sampled_width, down_sampled_height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-5);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
    }

    static void CalcPiAndPa(const std::string& file_path, const CARTA::FileType& file_type, int channel, int mip, bool debiasing,
        bool fractional, double threshold, double q_error, double u_error, int& down_sampled_width, int& down_sampled_height,
        std::vector<float>& pi, std::vector<float>& pa) {
        // Create the image reader
        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        std::vector<float> stokes_data_i = reader->ReadXY(channel, 0);
        std::vector<float> stokes_data_q = reader->ReadXY(channel, 1);
        std::vector<float> stokes_data_u = reader->ReadXY(channel, 2);

        // Block averaging, get down sampled data
        int image_width = reader->Width();
        int image_height = reader->Height();
        down_sampled_width = std::ceil((float)image_width / mip);
        down_sampled_height = std::ceil((float)image_height / mip);
        int down_sampled_area = down_sampled_height * down_sampled_width;

        std::vector<float> down_sampled_i(down_sampled_area);
        std::vector<float> down_sampled_q(down_sampled_area);
        std::vector<float> down_sampled_u(down_sampled_area);

        BlockSmooth(
            stokes_data_i.data(), down_sampled_i.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);
        BlockSmooth(
            stokes_data_q.data(), down_sampled_q.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);
        BlockSmooth(
            stokes_data_u.data(), down_sampled_u.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);

        // Reset Q and U errors as 0 if debiasing is not used
        if (!debiasing) {
            q_error = u_error = 0;
        }

        // Calculate PI, FPI, and PA
        auto calc_pi = [&](float q, float u) {
            if (!std::isnan(q) && !isnan(u)) {
                float result = sqrt(pow(q, 2) + pow(u, 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0);
                if (fractional) {
                    return result;
                } else {
                    if (result > threshold) {
                        return result;
                    }
                }
            }
            return std::numeric_limits<float>::quiet_NaN();
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

        auto reset_pa = [&](float pi, float pa) {
            if (std::isnan(pi)) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            return pa;
        };

        // Set PI/PA results
        pi.resize(down_sampled_area);
        pa.resize(down_sampled_area);

        // Calculate PI
        std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pi.begin(), calc_pi);

        if (fractional) { // Calculate FPI
            std::transform(down_sampled_i.begin(), down_sampled_i.end(), pi.begin(), pi.begin(), calc_fpi);
        }

        // Calculate PA
        std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pa.begin(), calc_pa);

        // Set NaN for PA if PI/FPI is NaN
        std::transform(pi.begin(), pi.end(), pa.begin(), pa.begin(), reset_pa);
    }

    static void GetDownSampledPixels(const std::string& file_path, const CARTA::FileType& file_type, int channel, int mip,
        int& down_sampled_width, int& down_sampled_height, std::vector<float>& pa) {
        // Create the image reader
        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        std::vector<float> image_data = reader->ReadXY(channel, -1);

        // Block averaging, get down sampled data
        int image_width = reader->Width();
        int image_height = reader->Height();
        down_sampled_width = std::ceil((float)image_width / mip);
        down_sampled_height = std::ceil((float)image_height / mip);
        int down_sampled_area = down_sampled_height * down_sampled_width;
        pa.resize(down_sampled_area);

        BlockSmooth(image_data.data(), pa.data(), image_width, image_height, down_sampled_width, down_sampled_height, 0, 0, mip);
    }

    static void TestStokesIntensityOrAngleSettings(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 0,
        int stokes_angle = 0) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        bool calculate_stokes_intensity(stokes_intensity >= 0);
        bool calculate_stokes_angle(stokes_angle >= 0);

        // Open a file in the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            if (calculate_stokes_intensity) {
                EXPECT_GE(response.intensity_tiles_size(), 1);
            } else {
                EXPECT_EQ(response.intensity_tiles_size(), 1);
            }

            if (calculate_stokes_angle) {
                EXPECT_GE(response.angle_tiles_size(), 1);
            } else {
                EXPECT_EQ(response.angle_tiles_size(), 1);
            }

            // Record progress
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback);

        CheckProgresses(progresses);
    }

    static std::pair<float, float> TestZFPCompression(std::string image_opts, const CARTA::FileType& file_type, int mip,
        float comprerssion_quality, bool fractional, bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0) {
        int stokes_intensity = 0;
        int stokes_angle = 0;

        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // Open a file in the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        int image_width = frame->Width();
        int image_height = frame->Height();
        int down_sampled_width = std::ceil((float)image_width / mip);
        int down_sampled_height = std::ceil((float)image_height / mip);
        int down_sampled_area = down_sampled_height * down_sampled_width;

        std::vector<float> pi_no_compression(down_sampled_area);
        std::vector<float> pa_no_compression(down_sampled_area);

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                // Fill PI values
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, down_sampled_width, pi_no_compression);
            }

            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                // Fill PA values
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, down_sampled_width, pa_no_compression);
            }
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback);

        // =============================================================================
        // Compress the vector field data with ZFP

        // Set the protobuf message
        auto message2 = Message::SetVectorOverlayParameters(0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity,
            stokes_angle, CARTA::CompressionType::ZFP, comprerssion_quality);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message2);

        // Set results data
        std::vector<float> pi_compression(down_sampled_area);
        std::vector<float> pa_compression(down_sampled_area);

        // Set callback function
        auto callback2 = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                // Fill PI values
                auto tile_pi = response.intensity_tiles(0);
                DecompressTileData(tile_pi, down_sampled_width, comprerssion_quality, pi_compression);
            }

            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                // Fill PA values
                auto tile_pa = response.angle_tiles(0);
                DecompressTileData(tile_pa, down_sampled_width, comprerssion_quality, pa_compression);
            }
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback2);

        // Check the absolute mean of error
        float pi_abs_err_mean = 0;
        int count_pi = 0;
        for (int i = 0; i < down_sampled_area; ++i) {
            if (!std::isnan(pi_no_compression[i]) && !std::isnan(pi_compression[i])) {
                pi_abs_err_mean += fabs(pi_no_compression[i] - pi_compression[i]);
                ++count_pi;
            }
        }
        EXPECT_GT(count_pi, 0);
        pi_abs_err_mean /= count_pi;

        float pa_abs_err_mean = 0;
        int count_pa = 0;
        for (int i = 0; i < down_sampled_area; ++i) {
            if (!std::isnan(pa_no_compression[i]) && !std::isnan(pa_compression[i])) {
                pa_abs_err_mean += fabs(pa_no_compression[i] - pa_compression[i]);
                ++count_pa;
            }
        }
        EXPECT_GT(count_pa, 0);
        pa_abs_err_mean /= count_pa;

        spdlog::info("For compression quality {}, the average of absolute errors for PI/PA are {}/{}.", comprerssion_quality,
            pi_abs_err_mean, pa_abs_err_mean);
        return std::make_pair(pi_abs_err_mean, pa_abs_err_mean);
    }

    static void DecompressTileData(
        const CARTA::TileData& tile, int down_sampled_width, float comprerssion_quality, std::vector<float>& array) {
        int tile_x = tile.x();
        int tile_y = tile.y();
        int tile_width = tile.width();
        int tile_height = tile.height();
        int tile_layer = tile.layer();
        std::vector<char> buf(tile.image_data().begin(), tile.image_data().end());

        // Decompress the data
        std::vector<float> val;
        Decompress(val, buf, tile_width, tile_height, comprerssion_quality);
        EXPECT_EQ(val.size(), tile_width * tile_height);

        for (int i = 0; i < val.size(); ++i) {
            int x = tile_x * TILE_SIZE + (i % tile_width);
            int y = tile_y * TILE_SIZE + (i / tile_width);
            array[y * down_sampled_width + x] = val[i];
        }
    }

    static void TestSessionVectorFieldCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 0,
        int stokes_angle = 0) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
        }

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int down_sampled_width;
        int down_sampled_height;
        std::vector<float> pi;
        std::vector<float> pa;
        CalcPiAndPa(file_path_string, file_type, channel, mip, debiasing, fractional, threshold, q_error, u_error, down_sampled_width,
            down_sampled_height, pi, pa);

        // =======================================================================================================
        // Calculate the vector field tile by tile with by the Session

        auto dummy_backend = BackendModel::GetDummyBackend();

        std::filesystem::path file_path(file_path_string);

        CARTA::OpenFile open_file = Message::OpenFile(file_path.parent_path(), file_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        dummy_backend->Receive(open_file);

        auto set_image_channels = Message::SetImageChannels(0, channel, 0, CARTA::CompressionType::ZFP, 11);

        dummy_backend->Receive(set_image_channels);

        dummy_backend->WaitForJobFinished();

        dummy_backend->ClearMessagesQueue();

        // Set the protobuf message
        auto set_vector_field_params = Message::SetVectorOverlayParameters(
            0, mip, fractional, debiasing, q_error, u_error, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        dummy_backend->Receive(set_vector_field_params);

        dummy_backend->WaitForJobFinished();

        // Set results data
        std::pair<std::vector<char>, bool> message_pair;
        std::vector<float> pi2(down_sampled_width * down_sampled_height);
        std::vector<float> pa2(down_sampled_width * down_sampled_height);
        std::vector<double> progresses;

        while (dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::VECTOR_OVERLAY_TILE_DATA) {
                auto response = Message::DecodeMessage<CARTA::VectorOverlayTileData>(message);
                EXPECT_EQ(response.intensity_tiles_size(), 1);
                if (response.intensity_tiles_size()) {
                    // Fill PI values
                    auto tile_pi = response.intensity_tiles(0);
                    GetTileData(tile_pi, down_sampled_width, pi2);
                }

                EXPECT_EQ(response.angle_tiles_size(), 1);
                if (response.angle_tiles_size()) {
                    // Fill PA values
                    auto tile_pa = response.angle_tiles(0);
                    GetTileData(tile_pa, down_sampled_width, pa2);
                }

                // Record progress
                progresses.push_back(response.progress());
            }
        }

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightAndBottomEdgeData(pi, pi2, pa, pa2, down_sampled_width, down_sampled_height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-5);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
    }

    static void TestImageWithNoStokesAxis(std::string image_shape, std::string image_opts, const CARTA::FileType& file_type, int mip,
        int stokes_intensity, int stokes_angle, double threshold = std::numeric_limits<double>::quiet_NaN()) {
        // Create the sample image
        std::string file_path_string;
        if (file_type == CARTA::FileType::HDF5) {
            file_path_string = ImageGenerator::GeneratedHdf5ImagePath(image_shape, image_opts);
        } else {
            file_path_string = ImageGenerator::GeneratedFitsImagePath(image_shape, image_opts);
        }

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int down_sampled_width;
        int down_sampled_height;
        std::vector<float> pixels;
        GetDownSampledPixels(file_path_string, file_type, channel, mip, down_sampled_width, down_sampled_height, pixels);

        // Apply a threshold cut
        if (!std::isnan(threshold)) {
            for (auto& pixel : pixels) {
                if (!std::isnan(pixel) && (pixel <= threshold)) {
                    pixel = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }

        // Check the threshold cut results
        for (auto pixel : pixels) {
            if (!std::isnan(pixel) && !std::isnan(threshold)) {
                EXPECT_GT(pixel, threshold);
            }
        }

        // =======================================================================================================
        // Calculate the vector field tile by tile with by the Session

        auto dummy_backend = BackendModel::GetDummyBackend();

        std::filesystem::path file_path(file_path_string);

        CARTA::OpenFile open_file = Message::OpenFile(file_path.parent_path(), file_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        dummy_backend->Receive(open_file);

        auto set_image_channels = Message::SetImageChannels(0, channel, 0, CARTA::CompressionType::ZFP, 11);

        dummy_backend->Receive(set_image_channels);

        dummy_backend->WaitForJobFinished();

        dummy_backend->ClearMessagesQueue();

        // Set the protobuf message
        auto set_vector_field_params = Message::SetVectorOverlayParameters(
            0, mip, false, false, 0, 0, threshold, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        dummy_backend->Receive(set_vector_field_params);

        dummy_backend->WaitForJobFinished();

        // Set results data
        std::pair<std::vector<char>, bool> message_pair;
        std::vector<float> pi(down_sampled_width * down_sampled_height);
        std::vector<float> pa2(down_sampled_width * down_sampled_height);
        std::vector<double> progresses;

        while (dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::VECTOR_OVERLAY_TILE_DATA) {
                auto response = Message::DecodeMessage<CARTA::VectorOverlayTileData>(message);
                if (stokes_intensity > -1) {
                    EXPECT_EQ(response.intensity_tiles_size(), 1);
                    if (response.intensity_tiles_size()) {
                        // Fill PI values
                        auto tile_pi = response.intensity_tiles(0);
                        GetTileData(tile_pi, down_sampled_width, pi);
                    }
                }
                if (stokes_angle > -1) {
                    EXPECT_EQ(response.angle_tiles_size(), 1);
                    if (response.angle_tiles_size()) {
                        // Fill PA values
                        auto tile_pa = response.angle_tiles(0);
                        GetTileData(tile_pa, down_sampled_width, pa2);
                    }
                }

                // Record progress
                progresses.push_back(response.progress());
            }
        }

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            if (stokes_intensity > -1) {
                CmpVectors(pixels, pi, 1e-6);
            }
            if (stokes_angle > -1) {
                CmpVectors(pixels, pa2, 1e-6);
            }
        } else {
            if (stokes_intensity > -1) {
                CmpVectors(pixels, pi);
            }
            if (stokes_angle > -1) {
                CmpVectors(pixels, pa2);
            }
        }
        CheckProgresses(progresses);
    }

    static void RemoveRightAndBottomEdgeData(std::vector<float>& pi, std::vector<float>& pi2, std::vector<float>& pa,
        std::vector<float>& pa2, int down_sampled_width, int down_sampled_height) {
        // For HDF5 files, if its down sampled data is calculated from the smaller mip (down sampled) data,
        // and the remainder of image width or height divided by this smaller mip is not 0.
        // Then the error would happen on the right or bottom edge of down sampled pixels compared to that down sampled from the full
        // resolution pixels. Because the "weight" of pixels for averaging in a mip X mip block are not equal.
        // In such case, we ignore the comparison of the data which on the right or bottom edge.

        // Remove the right edge data
        for (int i = 0; i < pi.size(); ++i) {
            if ((i + 1) % down_sampled_width == 0) {
                pi[i] = pi2[i] = std::numeric_limits<float>::quiet_NaN();
                pa[i] = pa2[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
        // Remove the bottom edge data
        for (int i = 0; i < pi.size(); ++i) {
            if (i / down_sampled_width == down_sampled_height - 1) {
                pi[i] = pi2[i] = std::numeric_limits<float>::quiet_NaN();
                pa[i] = pa2[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
};

TEST_F(VectorFieldTest, TestMipLayerConversion) {
    TestMipLayerConversion(1, 512, 1024);
    TestMipLayerConversion(2, 512, 1024);
    TestMipLayerConversion(4, 512, 1024);
    TestMipLayerConversion(8, 512, 1024);
    TestMipLayerConversion(16, 512, 1024);

    TestMipLayerConversion(1, 1024, 1024);
    TestMipLayerConversion(2, 1024, 1024);
    TestMipLayerConversion(4, 1024, 1024);
    TestMipLayerConversion(8, 1024, 1024);
    TestMipLayerConversion(16, 1024, 1024);

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

    TestRasterTilesGeneration(110, 110, 1);
    TestRasterTilesGeneration(110, 110, 2);
    TestRasterTilesGeneration(110, 110, 4);
    TestRasterTilesGeneration(110, 110, 8);
    TestRasterTilesGeneration(110, 110, 16);
}

TEST_F(VectorFieldTest, TestTilesData) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    int mip = 4;
    EXPECT_TRUE(TestTilesData(image_opts, file_type, "Ix", mip));
    EXPECT_TRUE(TestTilesData(image_opts, file_type, "Qx", mip));
    EXPECT_TRUE(TestTilesData(image_opts, file_type, "Ux", mip));
    EXPECT_TRUE(TestTilesData(image_opts, file_type, "Vx", mip));
}

TEST_F(VectorFieldTest, TestBlockSmooth) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    int mip = 4;
    EXPECT_TRUE(TestBlockSmooth(image_opts, file_type, "Ix", mip));
    EXPECT_TRUE(TestBlockSmooth(image_opts, file_type, "Qx", mip));
    EXPECT_TRUE(TestBlockSmooth(image_opts, file_type, "Ux", mip));
    EXPECT_TRUE(TestBlockSmooth(image_opts, file_type, "Vx", mip));
}

TEST_F(VectorFieldTest, TestTileCalc) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    bool fractional = false;
    double q_error = 1e-3;
    double u_error = 1e-3;
    double threshold = 1e-2;
    int mip = 4;
    EXPECT_TRUE(TestTileCalc(image_opts, file_type, mip, fractional, q_error, u_error, threshold));
}

TEST_F(VectorFieldTest, TestVectorFieldSettings) {
    VectorFieldSettings settings{2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings1{2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings2{4, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings3{2, false, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings4{2, true, 0.2, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings5{2, true, 0.1, false, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings6{2, true, 0.1, true, 0.02, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings7{2, true, 0.1, true, 0.01, 0.03, -1, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings8{2, true, 0.1, true, 0.01, 0.02, 0, -1, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings9{2, true, 0.1, true, 0.01, 0.02, -1, 0, CARTA::CompressionType::ZFP, 1};
    VectorFieldSettings settings10{2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::NONE, 1};
    VectorFieldSettings settings11{2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 2};
    EXPECT_TRUE(settings == settings1);
    EXPECT_TRUE(settings != settings2);
    EXPECT_TRUE(settings != settings3);
    EXPECT_TRUE(settings != settings4);
    EXPECT_TRUE(settings != settings5);
    EXPECT_TRUE(settings != settings6);
    EXPECT_TRUE(settings != settings7);
    EXPECT_TRUE(settings != settings8);
    EXPECT_TRUE(settings != settings9);
    EXPECT_TRUE(settings != settings10);
    EXPECT_TRUE(settings != settings11);
}

TEST_F(VectorFieldTest, TestVectorFieldCalc) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    bool fractional = false;
    bool debiasing = true;
    double q_error = 1e-3;
    double u_error = 1e-3;
    double threshold = 1e-2;
    int mip = 4;
    EXPECT_TRUE(TestVectorFieldCalc(image_opts, file_type, mip, fractional, debiasing, q_error, u_error, threshold));
}

TEST_F(VectorFieldTest, TestVectorFieldCalc2) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::HDF5;
    bool fractional = false;
    bool debiasing = true;
    double q_error = 1e-3;
    double u_error = 1e-3;
    double threshold = 1e-2;
    int mip = 4;
    TestVectorFieldCalc2(image_opts, file_type, mip, fractional, debiasing, q_error, u_error, threshold);
}

TEST_F(VectorFieldTest, TestStokesIntensityOrAngleSettings) {
    TestStokesIntensityOrAngleSettings(IMAGE_OPTS_NAN, CARTA::FileType::FITS, 4, true, false, 1e-3, 1e-3, 0.1, -1, 0);
    TestStokesIntensityOrAngleSettings(IMAGE_OPTS_NAN, CARTA::FileType::FITS, 4, true, false, 1e-3, 1e-3, 0.1, 0, -1);
    TestStokesIntensityOrAngleSettings(IMAGE_OPTS_NAN, CARTA::FileType::FITS, 4, true, false, 1e-3, 1e-3, 0.1, 0, 0);
    TestStokesIntensityOrAngleSettings(IMAGE_OPTS_NAN, CARTA::FileType::FITS, 4, true, false, 1e-3, 1e-3, 0.1, -1, -1);
}

TEST_F(VectorFieldTest, TestZFPCompression) {
    std::string image_opts = IMAGE_OPTS;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    int mip = 4;
    bool fractional = true;
    bool debiasing = false;
    std::pair<float, float> errors = TestZFPCompression(image_opts, file_type, mip, 10, fractional, debiasing);

    for (int compression_quality = 11; compression_quality < 22; ++compression_quality) {
        auto tmp_errors = TestZFPCompression(image_opts, file_type, mip, (float)compression_quality, fractional, debiasing);
        EXPECT_GT(errors.first, tmp_errors.first);
        EXPECT_GT(errors.second, tmp_errors.second);
        errors = tmp_errors;
    }
}

TEST_F(VectorFieldTest, TestImageWithNoStokesAxis) {
    CARTA::FileType file_type = CARTA::FileType::FITS;
    int mip = 4;
    double threshold = 0.0;
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, -1, 0);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, -1);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, 0);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, -1, 0);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, -1);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, 0);

    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, -1, 0, threshold);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, -1, threshold);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, 0, threshold);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, -1, 0, threshold);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, -1, threshold);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, 0, threshold);
}

TEST_F(VectorFieldTest, TestSessionVectorFieldCalc) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    bool fractional = false;
    bool debiasing = true;
    double q_error = 1e-3;
    double u_error = 1e-3;
    double threshold = 1e-2;
    int mip = 12;
    TestSessionVectorFieldCalc(image_opts, file_type, mip, fractional, debiasing, q_error, u_error, threshold);
}

TEST_F(VectorFieldTest, TestHdf5DownSampledData) {
    std::string image_opts = IMAGE_OPTS;
    CARTA::FileType file_type = CARTA::FileType::HDF5;
    bool fractional = false;
    bool debiasing = true;
    double q_error = 1e-3;
    double u_error = 1e-3;
    double threshold = 1e-2;
    int mip = 12;
    TestSessionVectorFieldCalc(image_opts, file_type, mip, fractional, debiasing, q_error, u_error, threshold);
}

TEST_F(VectorFieldTest, TestLoaderDownSampledData) {
    int image_width = 1110;
    int image_height = 1110;
    std::string image_shape = fmt::format("{} {} 25 4", image_width, image_height);
    std::string image_opts = IMAGE_OPTS; // or IMAGE_OPTS_NAN
    float abs_error = 1e-6;              // or 0.3
    int mip = 12;
    std::vector<int> loader_mips;

    EXPECT_TRUE(TestLoaderDownSampledData(image_shape, image_opts, "Ix", loader_mips));

    for (auto loader_mip : loader_mips) {
        if (mip % loader_mip == 0) {
            EXPECT_TRUE(TestBlockSmoothDownSampledData(image_shape, image_opts, "Ix", mip, loader_mip, abs_error));
        }
    }
}
