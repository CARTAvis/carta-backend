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

static const std::string IMAGE_SHAPE = "1110 1110 25 4";
static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class VectorFieldTest : public ::testing::Test {
public:
    static bool TestTilesData(std::string sample_file_path, std::string stokes_type, int mip) {
        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(sample_file_path), "0"));

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
        std::vector<float> image_data;
        if (!frame->GetSlicerData(section, image_data)) {
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

            std::vector<float> tile_data;
            if (!frame->GetSlicerData(tile_section, tile_data)) {
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

    static bool TestBlockSmooth(std::string sample_file_path, std::string stokes_type, int mip) {
        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(sample_file_path), "0"));

        // Get stokes image data
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        casacore::Slicer section = frame->GetImageSlicer(AxisRange(frame->CurrentZ()), stokes);
        std::vector<float> image_data;
        if (!frame->GetSlicerData(section, image_data)) {
            return false;
        }

        // Original image data size
        int image_width = frame->Width();
        int image_height = frame->Height();

        // Block averaging
        int x = 0;
        int y = 0;
        int req_height = image_height - y;
        int req_width = image_width - x;
        int down_sampled_height = std::ceil((float)req_height / mip);
        int down_sampled_width = std::ceil((float)req_width / mip);
        std::vector<float> down_sampled_data(down_sampled_height * down_sampled_width);

        BlockSmooth(
            image_data.data(), down_sampled_data.data(), image_width, image_height, down_sampled_width, down_sampled_height, x, y, mip);

        CheckDownSampledData(image_data, down_sampled_data, image_width, image_height, down_sampled_width, down_sampled_height, mip);

        return true;
    }

    static bool TestTileCalculations(
        std::string sample_file_path, int mip, bool fractional, double q_error = 0, double u_error = 0, double threshold = 0) {
        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(sample_file_path), "0"));

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

            std::vector<float> tile_data_i;
            std::vector<float> tile_data_q;
            std::vector<float> tile_data_u;

            if (!frame->GetSlicerData(tile_section_i, tile_data_i) || !frame->GetSlicerData(tile_section_q, tile_data_q) ||
                !frame->GetSlicerData(tile_section_u, tile_data_u)) {
                return false;
            }

            EXPECT_GT(tile_data_i.size(), 0);
            EXPECT_GT(tile_data_q.size(), 0);
            EXPECT_GT(tile_data_u.size(), 0);
            EXPECT_EQ(tile_data_i.size(), tile_original_width * tile_original_height);
            EXPECT_EQ(tile_data_q.size(), tile_original_width * tile_original_height);
            EXPECT_EQ(tile_data_u.size(), tile_original_width * tile_original_height);

            // Block averaging, get down sampled data
            int x = 0;
            int y = 0;
            int req_height = tile_original_height - y;
            int req_width = tile_original_width - x;
            int down_sampled_height = std::ceil((float)req_height / mip);
            int down_sampled_width = std::ceil((float)req_width / mip);
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
                down_sampled_height, x, y, mip);
            BlockSmooth(tile_data_q.data(), down_sampled_q.data(), tile_original_width, tile_original_height, down_sampled_width,
                down_sampled_height, x, y, mip);
            BlockSmooth(tile_data_u.data(), down_sampled_u.data(), tile_original_width, tile_original_height, down_sampled_width,
                down_sampled_height, x, y, mip);

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
            auto& tiles_pi = tiles_data_pi[i];
            tiles_pi.set_x(tiles[i].x);
            tiles_pi.set_y(tiles[i].y);
            tiles_pi.set_layer(tiles[i].layer);
            tiles_pi.set_width(down_sampled_width);
            tiles_pi.set_height(down_sampled_height);
            tiles_pi.set_image_data(pi.data(), sizeof(float) * pi.size());

            auto& tiles_pa = tiles_data_pa[i];
            tiles_pa.set_x(tiles[i].x);
            tiles_pa.set_y(tiles[i].y);
            tiles_pa.set_layer(tiles[i].layer);
            tiles_pa.set_width(down_sampled_width);
            tiles_pa.set_height(down_sampled_height);
            tiles_pa.set_image_data(pa.data(), sizeof(float) * pa.size());
        }

        // Check tiles protobuf data
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile_pi = tiles_data_pi[i];
            std::string buf_pi = tile_pi.image_data();
            std::vector<float> val_pi(buf_pi.size() / sizeof(float));
            memcpy(val_pi.data(), buf_pi.data(), buf_pi.size());

            auto& tile_pa = tiles_data_pa[i];
            std::string buf_pa = tile_pa.image_data();
            std::vector<float> val_pa(buf_pa.size() / sizeof(float));
            memcpy(val_pa.data(), buf_pa.data(), buf_pa.size());

            auto& pi = pis[i];
            auto& pa = pas[i];

            EXPECT_EQ(val_pi.size(), val_pa.size());
            EXPECT_EQ(pi.size(), pa.size());
            EXPECT_EQ(val_pi.size(), pi.size());
            EXPECT_EQ(val_pa.size(), pa.size());

            for (int j = 0; j < pi.size(); ++j) {
                if (!std::isnan(pi[j]) || !std::isnan(val_pi[j])) {
                    EXPECT_FLOAT_EQ(pi[j], val_pi[j]);
                }
                if (!std::isnan(pa[j]) || !std::isnan(val_pa[j])) {
                    EXPECT_FLOAT_EQ(pa[j], val_pa[j]);
                }
            }
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

    static bool TestFrameVectorField(
        std::string sample_file_path, int mip, bool fractional, double q_error = 0, double u_error = 0, double threshold = 0) {
        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(sample_file_path), "0"));

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

        std::vector<float> stokes_data_i;
        std::vector<float> stokes_data_q;
        std::vector<float> stokes_data_u;

        if (!frame->GetSlicerData(stokes_section_i, stokes_data_i) || !frame->GetSlicerData(stokes_section_q, stokes_data_q) ||
            !frame->GetSlicerData(stokes_section_u, stokes_data_u)) {
            return false;
        }

        // Block averaging, get down sampled data
        int x = 0;
        int y = 0;
        int req_width = image_width - x;
        int req_height = image_height - y;
        int down_sampled_width = std::ceil((float)req_width / mip);
        int down_sampled_height = std::ceil((float)req_height / mip);
        int down_sampled_area = down_sampled_height * down_sampled_width;

        std::vector<float> down_sampled_i(down_sampled_area);
        std::vector<float> down_sampled_q(down_sampled_area);
        std::vector<float> down_sampled_u(down_sampled_area);

        BlockSmooth(
            stokes_data_i.data(), down_sampled_i.data(), image_width, image_height, down_sampled_width, down_sampled_height, x, y, mip);
        BlockSmooth(
            stokes_data_q.data(), down_sampled_q.data(), image_width, image_height, down_sampled_width, down_sampled_height, x, y, mip);
        BlockSmooth(
            stokes_data_u.data(), down_sampled_u.data(), image_width, image_height, down_sampled_width, down_sampled_height, x, y, mip);

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

        // Set the protobuf message
        CARTA::SetVectorOverlayParameters message;
        message.set_smoothing_factor(mip);
        message.set_fractional(fractional);
        message.set_threshold(threshold);
        bool debiasing((q_error != 0) || (u_error != 0));
        message.set_debiasing(debiasing);
        message.set_q_error(q_error);
        message.set_u_error(u_error);
        message.set_stokes_intensity(0);
        message.set_stokes_angle(0);
        message.set_compression_quality(CARTA::CompressionType::NONE);
        message.set_compression_quality(0);

        // Set vector field parameters
        frame->SetVectorFieldParameters(message);

        // Set results data
        std::vector<float> pi_data(down_sampled_area);
        std::vector<float> pa_data(down_sampled_area);
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](const CARTA::TileData& tile_pi, const CARTA::TileData& tile_pa, double progress) {
            // Fill PI values
            int tile_pi_x = tile_pi.x();
            int tile_pi_y = tile_pi.y();
            int tile_pi_width = tile_pi.width();
            int tile_pi_height = tile_pi.height();
            int tile_pi_layer = tile_pi.layer();
            std::string buf_pi = tile_pi.image_data();
            std::vector<float> val_pi(buf_pi.size() / sizeof(float));
            memcpy(val_pi.data(), buf_pi.data(), buf_pi.size());

            for (int i = 0; i < val_pi.size(); ++i) {
                int x = tile_pi_x * TILE_SIZE + (i % tile_pi_width);
                int y = tile_pi_y * TILE_SIZE + (i / tile_pi_width);
                pi_data[y * down_sampled_width + x] = val_pi[i];
            }

            // Fill PA values
            int tile_pa_x = tile_pa.x();
            int tile_pa_y = tile_pa.y();
            int tile_pa_width = tile_pa.width();
            int tile_pa_height = tile_pa.height();
            int tile_pa_layer = tile_pa.layer();
            std::string buf_pa = tile_pa.image_data();
            std::vector<float> val_pa(buf_pa.size() / sizeof(float));
            memcpy(val_pa.data(), buf_pa.data(), buf_pa.size());

            for (int i = 0; i < val_pa.size(); ++i) {
                int x = tile_pa_x * TILE_SIZE + (i % tile_pa_width);
                int y = tile_pa_y * TILE_SIZE + (i / tile_pa_width);
                pa_data[y * down_sampled_width + x] = val_pa[i];
            }

            // Record progress
            progresses.push_back(progress);
        };

        // Do PI/PA calculations by the Frame function
        frame->VectorFieldImage(callback);

        // Check results
        EXPECT_EQ(pi.size(), pi_data.size());
        for (int i = 0; i < pi.size(); ++i) {
            if (!std::isnan(pi[i]) || !std::isnan(pi_data[i])) {
                EXPECT_FLOAT_EQ(pi[i], pi_data[i]);
            }
        }
        EXPECT_EQ(pa.size(), pa_data.size());
        for (int i = 0; i < pa.size(); ++i) {
            if (!std::isnan(pa[i]) || !std::isnan(pa_data[i])) {
                EXPECT_FLOAT_EQ(pa[i], pa_data[i]);
            }
        }
        EXPECT_EQ(progresses.back(), 1);
        return true;
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
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestTilesData(sample_file, "Ix", 1));
    EXPECT_TRUE(TestTilesData(sample_file, "Ix", 2));
    EXPECT_TRUE(TestTilesData(sample_file, "Ix", 4));
    EXPECT_TRUE(TestTilesData(sample_file, "Ix", 8));
    EXPECT_TRUE(TestTilesData(sample_file, "Ix", 16));

    EXPECT_TRUE(TestTilesData(sample_file, "Qx", 1));
    EXPECT_TRUE(TestTilesData(sample_file, "Qx", 2));
    EXPECT_TRUE(TestTilesData(sample_file, "Qx", 4));
    EXPECT_TRUE(TestTilesData(sample_file, "Qx", 8));
    EXPECT_TRUE(TestTilesData(sample_file, "Qx", 16));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Ix", 1));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Ix", 2));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Ix", 4));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Ix", 8));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Ix", 16));

    EXPECT_TRUE(TestTilesData(sample_nan_file, "Qx", 1));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Qx", 2));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Qx", 4));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Qx", 8));
    EXPECT_TRUE(TestTilesData(sample_nan_file, "Qx", 16));
}

TEST_F(VectorFieldTest, TestBlockSmooth) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ix", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Qx", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ux", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Vx", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ix", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Qx", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ux", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Vx", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ix", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Qx", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ux", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Vx", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ix", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Qx", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ux", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Vx", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ix", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Qx", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Ux", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_file, "Vx", 16));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ix", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Qx", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ux", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Vx", 1));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ix", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Qx", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ux", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Vx", 2));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ix", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Qx", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ux", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Vx", 4));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ix", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Qx", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ux", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Vx", 8));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ix", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Qx", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Ux", 16));
    EXPECT_TRUE(TestBlockSmooth(sample_nan_file, "Vx", 16));
}

TEST_F(VectorFieldTest, TestTileCalculations) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestTileCalculations(sample_file, 1, true));
    EXPECT_TRUE(TestTileCalculations(sample_file, 2, true));
    EXPECT_TRUE(TestTileCalculations(sample_file, 4, true));
    EXPECT_TRUE(TestTileCalculations(sample_file, 8, true));
    EXPECT_TRUE(TestTileCalculations(sample_file, 16, true));
    EXPECT_TRUE(TestTileCalculations(sample_file, 1, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 2, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 4, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 8, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 16, true, 1e-3, 1e-3, 0.1));

    EXPECT_TRUE(TestTileCalculations(sample_file, 1, false));
    EXPECT_TRUE(TestTileCalculations(sample_file, 2, false));
    EXPECT_TRUE(TestTileCalculations(sample_file, 4, false));
    EXPECT_TRUE(TestTileCalculations(sample_file, 8, false));
    EXPECT_TRUE(TestTileCalculations(sample_file, 16, false));
    EXPECT_TRUE(TestTileCalculations(sample_file, 1, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 2, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 4, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 8, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_file, 16, false, 1e-3, 1e-3, 0.1));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 1, true));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 2, true));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 4, true));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 8, true));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 16, true));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 1, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 2, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 4, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 8, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 16, true, 1e-3, 1e-3, 0.1));

    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 1, false));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 2, false));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 4, false));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 8, false));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 16, false));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 1, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 2, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 4, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 8, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestTileCalculations(sample_nan_file, 16, false, 1e-3, 1e-3, 0.1));
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

TEST_F(VectorFieldTest, TestFrameVectorField) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestFrameVectorField(sample_file, 1, true));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 2, true));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 4, true));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 8, true));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 16, true));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 1, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 2, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 4, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 8, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 16, true, 1e-3, 1e-3, 0.1));

    EXPECT_TRUE(TestFrameVectorField(sample_file, 1, false));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 2, false));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 4, false));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 8, false));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 16, false));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 1, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 2, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 4, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 8, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_file, 16, false, 1e-3, 1e-3, 0.1));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 1, true));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 2, true));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 4, true));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 8, true));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 16, true));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 1, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 2, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 4, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 8, true, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 16, true, 1e-3, 1e-3, 0.1));

    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 1, false));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 2, false));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 4, false));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 8, false));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 16, false));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 1, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 2, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 4, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 8, false, 1e-3, 1e-3, 0.1));
    EXPECT_TRUE(TestFrameVectorField(sample_nan_file, 16, false, 1e-3, 1e-3, 0.1));
}
