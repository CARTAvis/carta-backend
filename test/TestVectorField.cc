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
#include "Frame/VectorFieldCalculator.h"
#include "Session/Session.h"
#include "Util/Message.h"

#define FLOAT_NAN std::numeric_limits<float>::quiet_NaN()

static const std::string IMAGE_SHAPE = "1110 1110 25 4";
static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}

    std::vector<int> GetLoaderMips() {
        std::vector<int> results;
        for (int loader_mip = 0; loader_mip < 17; ++loader_mip) {
            if (_loader->HasMip(loader_mip)) {
                results.push_back(loader_mip);
            }
        }
        return results;
    }

    bool GetLoaderDownsampledData(std::vector<float>& data, int channel, int stokes, CARTA::ImageBounds& bounds, int mip) {
        if (!ImageBoundsValid(bounds)) {
            return false;
        }
        if (!_loader->HasMip(mip) || !_loader->GetDownsampledRasterData(data, channel, stokes, bounds, mip, _image_mutex)) {
            return false;
        }
        return true;
    }

    bool GetDownsampledData(
        std::vector<float>& data, int& width, int& height, int channel, int stokes, const CARTA::ImageBounds& bounds, int mip) {
        if (!ImageBoundsValid(bounds)) {
            return false;
        }

        // Get original raster tile data
        int x_min = bounds.x_min();
        int x_max = bounds.x_max() - 1;
        int y_min = bounds.y_min();
        int y_max = bounds.y_max() - 1;

        auto tile_section = GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes);
        std::vector<float> tile_data(tile_section.slicer.length().product());
        if (!GetSlicerData(tile_section, tile_data.data())) {
            return false;
        }

        int tile_original_width = bounds.x_max() - bounds.x_min();
        int tile_original_height = bounds.y_max() - bounds.y_min();
        width = std::ceil((float)tile_original_width / mip);
        height = std::ceil((float)tile_original_height / mip);

        // Get downsampled raster tile data by block averaging
        data.resize(height * width);
        return BlockSmooth(tile_data.data(), data.data(), tile_original_width, tile_original_height, width, height, 0, 0, mip);
    }

    static bool ImageBoundsValid(const CARTA::ImageBounds& bounds) {
        int tile_width = bounds.x_max() - bounds.x_min();
        int tile_height = bounds.y_max() - bounds.y_min();
        return ((tile_width > 0) && (tile_height > 0));
    }
};

class VectorFieldTest : public ::testing::Test {
    float _q_error = 0.0;
    float _u_error = 0.0;
    float _threshold = FLOAT_NAN;

    std::function<float(float, float)> _calc_pi = [this](float q, float u) {
        return ((IsValid(q, u)) ? (float)sqrt(pow(q, 2) + pow(u, 2) - (pow(_q_error, 2) + pow(_u_error, 2)) / 2.0) : FLOAT_NAN);
    };
    std::function<float(float, float)> _calc_fpi = [](float i, float pi) { return (IsValid(i, pi) ? (float)100.0 * (pi / i) : FLOAT_NAN); };
    std::function<float(float, float)> _calc_pa = [](float q, float u) {
        return (IsValid(q, u) ? (float)(180.0 / casacore::C::pi) * atan2(u, q) / 2 : FLOAT_NAN);
    };
    std::function<float(float, float)> _apply_threshold = [this](float i, float result) {
        return ((std::isnan(i) || (!std::isnan(_threshold) && (i < _threshold))) ? FLOAT_NAN : result);
    };

    static std::string GenerateImage(const CARTA::FileType& file_type, const std::string& image_opts) {
        if (file_type == CARTA::FileType::HDF5) {
            return ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, image_opts);
        }
        return ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, image_opts);
    }

    void SetErrorsThreshold(const float& q_error, const float& u_error, const float& threshold) {
        _q_error = q_error;
        _u_error = u_error;
        _threshold = threshold;
    }

    void CheckDownsampledData(const std::vector<float>& src_data, const std::vector<float>& dest_data, int src_width, int src_height,
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

    void CalcPiPa(const std::string& file_path, const CARTA::FileType& file_type, int channel, int mip, bool debiasing, bool fractional,
        double threshold, double q_error, double u_error, int& width, int& height, std::vector<float>& pi, std::vector<float>& pa) {
        SetErrorsThreshold(q_error, u_error, threshold);

        // Create the image reader
        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        // Initialize stokes maps
        std::unordered_map<std::string, int> stokes_indices{{"I", 0}, {"Q", 1}, {"U", 2}};
        std::unordered_map<std::string, std::vector<float>> data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};
        std::unordered_map<std::string, std::vector<float>> downsampled_data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};

        for (auto& one : data) {
            const auto& stokes_type = one.first;
            auto& tmp_data = one.second;
            tmp_data = reader->ReadXY(channel, stokes_indices[stokes_type]);
        }

        // Block averaging, get downsampled data
        int image_width = reader->Width();
        int image_height = reader->Height();
        width = std::ceil((float)image_width / mip);
        height = std::ceil((float)image_height / mip);
        int area = height * width;

        for (auto& one : downsampled_data) {
            const auto& stokes_type = one.first;
            auto& tmp_data = one.second;
            tmp_data.resize(area);
            BlockSmooth(data[stokes_type].data(), tmp_data.data(), image_width, image_height, width, height, 0, 0, mip);
        }

        // Reset Q and U errors as 0 if debiasing is not used
        if (!debiasing) {
            q_error = u_error = 0;
        }

        // Set PI/PA results
        pi.resize(area);
        pa.resize(area);

        // Calculate PI
        std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pi.begin(), _calc_pi);
        if (fractional) { // Calculate FPI
            std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _calc_fpi);
        }

        // Calculate PA
        std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pa.begin(), _calc_pa);

        // Set NaN for PI and PA if stokes I is NaN or below the threshold
        std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _apply_threshold);
        std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pa.begin(), pa.begin(), _apply_threshold);
    }

    static bool IsValid(double a, double b) {
        return (!std::isnan(a) && !std::isnan(b));
    }

    static void CheckProgresses(const std::vector<double>& progresses) {
        EXPECT_TRUE(!progresses.empty());
        if (!progresses.empty()) {
            EXPECT_EQ(progresses.back(), 1);
        }
    }

    static void GetTileData(const CARTA::TileData& tile, int downsampled_width, std::vector<float>& array) {
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
            array[y * downsampled_width + x] = val[i];
        }
    }

    static void DecompressTileData(
        const CARTA::TileData& tile, int downsampled_width, float comprerssion_quality, std::vector<float>& array) {
        int tile_x = tile.x();
        int tile_y = tile.y();
        int tile_width = tile.width();
        int tile_height = tile.height();
        int tile_layer = tile.layer();
        std::vector<char> buf(tile.image_data().begin(), tile.image_data().end());

        // Decompress the data
        std::vector<float> values;
        Decompress(values, buf, tile_width, tile_height, comprerssion_quality);
        EXPECT_EQ(values.size(), tile_width * tile_height);

        for (int i = 0; i < values.size(); ++i) {
            int x = tile_x * TILE_SIZE + (i % tile_width);
            int y = tile_y * TILE_SIZE + (i / tile_width);
            array[y * downsampled_width + x] = values[i];
        }
    }

    static void GetDownsampledPixels(const std::string& file_path, const CARTA::FileType& file_type, int channel, int stokes, int mip,
        int& width, int& height, std::vector<float>& pa) {
        // Create the image reader
        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        std::vector<float> image_data = reader->ReadXY(channel, stokes);

        // Block averaging, get downsampled data
        int image_width = reader->Width();
        int image_height = reader->Height();
        width = std::ceil((float)image_width / mip);
        height = std::ceil((float)image_height / mip);
        pa.resize(height * width);

        BlockSmooth(image_data.data(), pa.data(), image_width, image_height, width, height, 0, 0, mip);
    }

    static void RemoveRightBottomEdgeData(std::vector<float>& pi, std::vector<float>& pi2, std::vector<float>& pa, std::vector<float>& pa2,
        int downsampled_width, int downsampled_height) {
        // For HDF5 files, if its downsampled data is calculated from the smaller mip (downsampled) data, and the remainder of image width
        // or height divided by this smaller mip is not 0. The error would happen on the right or bottom edge of downsampled pixels compared
        // to that downsampled from the full resolution pixels. Because the "weight" of pixels for averaging in a mip X mip block are not
        // equal. In such case, we ignore the comparison of the data which on the right or bottom edge.

        // Remove the right edge data
        for (int i = 0; i < pi.size(); ++i) {
            if ((i + 1) % downsampled_width == 0) {
                pi[i] = pi2[i] = FLOAT_NAN;
                pa[i] = pa2[i] = FLOAT_NAN;
            }
        }
        // Remove the bottom edge data
        for (int i = 0; i < pi.size(); ++i) {
            if (i / downsampled_width == downsampled_height - 1) {
                pi[i] = pi2[i] = FLOAT_NAN;
                pa[i] = pa2[i] = FLOAT_NAN;
            }
        }
    }

public:
    static bool TestLoaderDownsampledData(
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
        CARTA::ImageBounds bounds = Message::ImageBounds(0, frame->Width(), 0, frame->Height());

        for (auto loader_mip : loader_mips) {
            // Get (HDF5) loader downsampled data
            std::vector<float> data1;
            if (!frame->GetLoaderDownsampledData(data1, channel, stokes, bounds, loader_mip)) {
                return false;
            }

            // Get downsampled data from the full resolution raster data
            std::vector<float> data2;
            int width, height;
            if (!frame->GetDownsampledData(data2, width, height, channel, stokes, bounds, loader_mip)) {
                return false;
            }

            // Compare two downsampled data
            CmpVectors(data1, data2, 1e-6);
        }
        return true;
    }

    bool TestBlockSmoothDownsampledData(
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
        CARTA::ImageBounds bounds = Message::ImageBounds(0, image_width, 0, image_height);

        // Get (HDF5) loader downsampled data
        std::vector<float> loader_data;
        if (!frame->GetLoaderDownsampledData(loader_data, channel, stokes, bounds, loader_mip)) {
            return false;
        }

        // Get downsampled data from the smaller loader downsampled data
        int width_1st = std::ceil((float)image_width / loader_mip);
        int height_1st = std::ceil((float)image_height / loader_mip);
        int mip_2nd = mip / loader_mip;
        int width_2nd = std::ceil((float)width_1st / mip_2nd);
        int height_2nd = std::ceil((float)height_1st / mip_2nd);
        std::vector<float> data1(height_2nd * width_2nd);
        if (!BlockSmooth(loader_data.data(), data1.data(), width_1st, height_1st, width_2nd, height_2nd, 0, 0, mip_2nd)) {
            return false;
        }

        // Check does the function BlockSmooth work well
        CheckDownsampledData(loader_data, data1, width_1st, height_1st, width_2nd, height_2nd, mip_2nd);

        // Get downsampled data from the full resolution raster data
        std::vector<float> data2;
        int width, height;
        if (!frame->GetDownsampledData(data2, width, height, channel, stokes, bounds, mip)) {
            return false;
        }
        EXPECT_EQ(width, width_2nd);
        EXPECT_EQ(height, height_2nd);

        if (image_width % loader_mip != 0) {
            // Remove the right edge pixels
            for (int i = 0; i < data2.size(); ++i) {
                if ((i + 1) % width == 0) {
                    data1[i] = data2[i] = FLOAT_NAN;
                }
            }
        }

        if (image_height % loader_mip != 0) {
            // Remove the bottom edge pixels
            for (int i = 0; i < data2.size(); ++i) {
                if (i / width == height - 1) {
                    data1[i] = data2[i] = FLOAT_NAN;
                }
            }
        }

        // Compare two downsampled data
        CmpVectors(data1, data2, abs_error);
        return true;
    }

    bool TestTilesData(std::string image_opts, const CARTA::FileType& file_type, std::string stokes_type, int mip) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

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
        auto section = frame->GetImageSlicer(AxisRange(channel), stokes);
        std::vector<float> image_data(section.slicer.length().product());
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
            EXPECT_GT(tile_original_width, 0);
            EXPECT_GT(tile_original_height, 0);

            int x_min = bounds.x_min();
            int x_max = bounds.x_max() - 1;
            int y_min = bounds.y_min();
            int y_max = bounds.y_max() - 1;
            auto tile_section = frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes);

            std::vector<float> tile_data(tile_section.slicer.length().product());
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
                if (IsValid(image_data[image_index], tile_data[j])) {
                    EXPECT_FLOAT_EQ(image_data[image_index], tile_data[j]);
                }
                ++count;
            }
        }
        EXPECT_EQ(image_data.size(), count);
        return true;
    }

    bool TestBlockSmooth(std::string image_opts, const CARTA::FileType& file_type, std::string stokes_type, int mip) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Get stokes image data
        int stokes;
        if (!frame->GetStokesTypeIndex(stokes_type, stokes)) {
            return false;
        }

        auto section = frame->GetImageSlicer(AxisRange(frame->CurrentZ()), stokes);
        std::vector<float> image_data(section.slicer.length().product());
        if (!frame->GetSlicerData(section, image_data.data())) {
            return false;
        }

        // Original image data size
        int image_width = frame->Width();
        int image_height = frame->Height();

        // Block averaging
        int height = std::ceil((float)image_height / mip);
        int width = std::ceil((float)image_width / mip);
        std::vector<float> data(height * width);
        BlockSmooth(image_data.data(), data.data(), image_width, image_height, width, height, 0, 0, mip);
        CheckDownsampledData(image_data, data, image_width, image_height, width, height, mip);
        return true;
    }

    bool TestTileCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional, double q_error = 0,
        double u_error = 0, double threshold = 0) {
        SetErrorsThreshold(q_error, u_error, threshold);

        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(file_path_string), "0"));

        // Initialize stokes maps
        std::unordered_map<std::string, int> stokes_indices{{"I", -1}, {"Q", -1}, {"U", -1}};
        std::unordered_map<std::string, StokesSlicer> stokes_slicers{{"I", StokesSlicer()}, {"Q", StokesSlicer()}, {"U", StokesSlicer()}};
        std::unordered_map<std::string, std::vector<float>> data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};
        std::unordered_map<std::string, std::vector<float>> downsampled_data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};

        // Get Stokes I, Q, and U indices
        for (auto& one : stokes_indices) {
            const auto& stokes_type = one.first;
            auto& stokes_index = one.second;
            if (!frame->GetStokesTypeIndex(stokes_type + "x", stokes_index)) {
                return false;
            }
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

            for (auto& one : stokes_slicers) {
                const auto& stokes_type = one.first;
                auto& stokes_slicer = one.second;
                stokes_slicer = frame->GetImageSlicer(
                    AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_indices[stokes_type]);
            }

            for (auto& one : data) {
                const auto& stokes_type = one.first;
                auto& tmp_data = one.second;
                tmp_data.resize(stokes_slicers[stokes_type].slicer.length().product());
                if (!frame->GetSlicerData(stokes_slicers[stokes_type], tmp_data.data())) {
                    return false;
                }
                EXPECT_EQ(tmp_data.size(), tile_original_width * tile_original_height);
            }

            // Block averaging, get downsampled data
            int height = std::ceil((float)tile_original_height / mip);
            int width = std::ceil((float)tile_original_width / mip);
            int area = height * width;

            if (mip > 1) {
                EXPECT_GT(tile_original_width, width);
                EXPECT_GT(tile_original_height, height);
            } else {
                EXPECT_EQ(tile_original_width, width);
                EXPECT_EQ(tile_original_height, height);
            }

            for (auto& one : downsampled_data) {
                const auto& stokes_type = one.first;
                auto& tmp_data = one.second;
                tmp_data.resize(area);
                BlockSmooth(data[stokes_type].data(), tmp_data.data(), tile_original_width, tile_original_height, width, height, 0, 0, mip);
                CheckDownsampledData(data[stokes_type], tmp_data, tile_original_width, tile_original_height, width, height, mip);
            }

            // Set PI/PA results
            auto& pi = pis[i];
            auto& pa = pas[i];
            pi.resize(area);
            pa.resize(area);

            // Calculate PI
            std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pi.begin(), _calc_pi);
            if (fractional) { // Calculate FPI
                std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _calc_fpi);
            }

            // Calculate PA
            std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pa.begin(), _calc_pa);

            // Set NaN for PI and PA if stokes I is NaN or below the threshold
            std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _apply_threshold);
            std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pa.begin(), pa.begin(), _apply_threshold);

            // Check calculation results
            for (int j = 0; j < area; ++j) {
                float expected_pi;
                if (fractional) {
                    expected_pi = sqrt(pow(downsampled_data["Q"][j], 2) + pow(downsampled_data["U"][j], 2) -
                                       (pow(q_error, 2) + pow(u_error, 2)) / 2.0) /
                                  downsampled_data["I"][j];
                } else {
                    expected_pi = sqrt(
                        pow(downsampled_data["Q"][j], 2) + pow(downsampled_data["U"][j], 2) - (pow(q_error, 2) + pow(u_error, 2)) / 2.0);
                }

                float expected_pa = (float)(180.0 / casacore::C::pi) * atan2(downsampled_data["U"][j], downsampled_data["Q"][j]) / 2;

                expected_pi = (downsampled_data["I"][j] >= threshold) ? expected_pi : FLOAT_NAN;
                expected_pa = (downsampled_data["I"][j] >= threshold) ? expected_pa : FLOAT_NAN;

                if (IsValid(pi[j], expected_pi)) {
                    EXPECT_FLOAT_EQ(pi[j], expected_pi);
                }
                if (IsValid(pa[j], expected_pa)) {
                    EXPECT_FLOAT_EQ(pa[j], expected_pa);
                }
            }

            // Fill tiles protobuf data
            auto& tile_pi = tiles_data_pi[i];
            FillTileData(&tile_pi, tiles[i].x, tiles[i].y, tiles[i].layer, mip, width, height, pi, CARTA::CompressionType::NONE, 0);

            auto& tile_pa = tiles_data_pa[i];
            FillTileData(&tile_pa, tiles[i].x, tiles[i].y, tiles[i].layer, mip, width, height, pa, CARTA::CompressionType::NONE, 0);
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

    static void TestMipLayerConversion(int mip, int image_width, int image_height) {
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

    bool TestVectorFieldCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional, bool debiasing = true,
        double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 1, int stokes_angle = 1) {
        SetErrorsThreshold(q_error, u_error, threshold);

        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // Open the file
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(0, loaders.Get(file_path_string), "0");

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        // Initialize stokes maps
        std::unordered_map<std::string, int> stokes_indices{{"I", -1}, {"Q", -1}, {"U", -1}};
        std::unordered_map<std::string, StokesSlicer> stokes_slicers{{"I", StokesSlicer()}, {"Q", StokesSlicer()}, {"U", StokesSlicer()}};
        std::unordered_map<std::string, std::vector<float>> data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};
        std::unordered_map<std::string, std::vector<float>> downsampled_data{
            {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}};

        // Get Stokes I, Q, and U indices
        for (auto& stokes : stokes_indices) {
            const auto& stokes_type = stokes.first;
            auto& stokes_index = stokes.second;
            if (!frame->GetStokesTypeIndex(stokes_type + "x", stokes_index)) {
                return false;
            }
        }

        int channel = frame->CurrentZ();
        int image_width = frame->Width();
        int image_height = frame->Height();

        // Get raster tile data
        int x_min = 0;
        int x_max = image_width - 1;
        int y_min = 0;
        int y_max = image_height - 1;

        for (auto& one : stokes_slicers) {
            const auto& stokes_type = one.first;
            auto& stokes_slicer = one.second;
            stokes_slicer =
                frame->GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(channel), stokes_indices[stokes_type]);
        }

        for (auto& one : data) {
            const auto& stokes_type = one.first;
            auto& tmp_data = one.second;
            tmp_data.resize(stokes_slicers[stokes_type].slicer.length().product());
            if (!frame->GetSlicerData(stokes_slicers[stokes_type], tmp_data.data())) {
                return false;
            }
            EXPECT_EQ(tmp_data.size(), image_width * image_height);
        }

        // Block averaging, get downsampled data
        int width = std::ceil((float)image_width / mip);
        int height = std::ceil((float)image_height / mip);
        int area = height * width;

        for (auto& one : downsampled_data) {
            const auto& stokes_type = one.first;
            auto& tmp_data = one.second;
            tmp_data.resize(area);
            BlockSmooth(data[stokes_type].data(), tmp_data.data(), image_width, image_height, width, height, 0, 0, mip);
            CheckDownsampledData(data[stokes_type], tmp_data, image_width, image_height, width, height, mip);
        }

        // Reset Q and U errors as 0 if debiasing is not used
        if (!debiasing) {
            q_error = u_error = 0;
        }

        // Set PI/PA results
        std::vector<float> pi(area), pa(area);

        // Calculate PI
        std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pi.begin(), _calc_pi);
        if (fractional) { // Calculate FPI
            std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _calc_fpi);
        }

        // Calculate PA
        std::transform(downsampled_data["Q"].begin(), downsampled_data["Q"].end(), downsampled_data["U"].begin(), pa.begin(), _calc_pa);

        // Set NaN for PI and PA if stokes I is NaN or below the threshold
        std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pi.begin(), pi.begin(), _apply_threshold);
        std::transform(downsampled_data["I"].begin(), downsampled_data["I"].end(), pa.begin(), pa.begin(), _apply_threshold);

        // =======================================================================================================
        // Calculate the vector field tile by tile with the Frame function

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        std::vector<float> pi2(area), pa2(area);
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, width, pi2);
            }
            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, width, pa2);
            }
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        VectorFieldCalculator vector_field_calculator(frame);
        vector_field_calculator.DoCalculations(callback);

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightBottomEdgeData(pi, pi2, pa, pa2, width, height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-5);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
        return true;
    }

    void TestVectorFieldCalc2(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional, bool debiasing = true,
        double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 1, int stokes_angle = 1) {
        // Create the sample image
        std::string file_path = GenerateImage(file_type, image_opts);

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int width, height;
        std::vector<float> pi, pa;
        CalcPiPa(file_path, file_type, channel, mip, debiasing, fractional, threshold, q_error, u_error, width, height, pi, pa);

        // =======================================================================================================
        // Calculate the vector field tile by tile with the Frame function

        // Open file with the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(0, loaders.Get(file_path), "0");

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        std::vector<float> pi2(width * height), pa2(width * height);
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, width, pi2);
            }
            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, width, pa2);
            }
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        VectorFieldCalculator vector_field_calculator(frame);
        vector_field_calculator.DoCalculations(callback);

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightBottomEdgeData(pi, pi2, pa, pa2, width, height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-4);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
    }

    static void TestStokesIntensityOrAngleSettings(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 1,
        int stokes_angle = 1) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // Open a file in the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(0, loaders.Get(file_path_string), "0");

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        int intensity_tiles_size = 0;
        int angle_tiles_size = 0;
        std::vector<double> progresses;

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            intensity_tiles_size = response.intensity_tiles_size();
            angle_tiles_size = response.angle_tiles_size();
            progresses.push_back(response.progress());
        };

        // Do PI/PA calculations by the Frame function
        VectorFieldCalculator vector_field_calculator(frame);
        vector_field_calculator.DoCalculations(callback);

        if (stokes_intensity > -1) {
            EXPECT_GE(intensity_tiles_size, 1);
        } else {
            EXPECT_EQ(intensity_tiles_size, 1);
        }

        if (stokes_angle > -1) {
            EXPECT_GE(angle_tiles_size, 1);
        } else {
            EXPECT_EQ(angle_tiles_size, 1);
        }

        CheckProgresses(progresses);
    }

    std::pair<float, float> TestZFPCompression(std::string image_opts, const CARTA::FileType& file_type, int mip,
        float comprerssion_quality, bool fractional, bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0,
        int stokes_intensity = 1, int stokes_angle = 1) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // Open a file in the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(0, loaders.Get(file_path_string), "0");

        // Set the protobuf message
        auto message = Message::SetVectorOverlayParameters(
            0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message);

        // Set results data
        int image_width = frame->Width();
        int image_height = frame->Height();
        int width = std::ceil((float)image_width / mip);
        int height = std::ceil((float)image_height / mip);
        int area = height * width;

        std::vector<float> pi_no_compression(area), pa_no_compression(area);

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                auto tile_pi = response.intensity_tiles(0);
                GetTileData(tile_pi, width, pi_no_compression);
            }

            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                auto tile_pa = response.angle_tiles(0);
                GetTileData(tile_pa, width, pa_no_compression);
            }
        };

        // Do PI/PA calculations by the Frame function
        VectorFieldCalculator vector_field_calculator(frame);
        vector_field_calculator.DoCalculations(callback);

        // =============================================================================
        // Compress the vector field data with ZFP

        // Set the protobuf message
        auto message2 = Message::SetVectorOverlayParameters(0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity,
            stokes_angle, CARTA::CompressionType::ZFP, comprerssion_quality);

        // Set vector field parameters
        frame->SetVectorOverlayParameters(message2);

        // Set results data
        std::vector<float> pi_compression(area), pa_compression(area);

        // Set callback function
        auto callback2 = [&](CARTA::VectorOverlayTileData& response) {
            EXPECT_EQ(response.intensity_tiles_size(), 1);
            if (response.intensity_tiles_size()) {
                auto tile_pi = response.intensity_tiles(0);
                DecompressTileData(tile_pi, width, comprerssion_quality, pi_compression);
            }
            EXPECT_EQ(response.angle_tiles_size(), 1);
            if (response.angle_tiles_size()) {
                auto tile_pa = response.angle_tiles(0);
                DecompressTileData(tile_pa, width, comprerssion_quality, pa_compression);
            }
        };

        // Do PI/PA calculations by the Frame function
        vector_field_calculator.DoCalculations(callback2);

        // Check the absolute mean of error
        float pi_abs_err_mean = 0;
        int count_pi = 0;
        for (int i = 0; i < area; ++i) {
            if (IsValid(pi_no_compression[i], pi_compression[i])) {
                pi_abs_err_mean += fabs(pi_no_compression[i] - pi_compression[i]);
                ++count_pi;
            }
        }
        EXPECT_GT(count_pi, 0);
        pi_abs_err_mean /= count_pi;

        float pa_abs_err_mean = 0;
        int count_pa = 0;
        for (int i = 0; i < area; ++i) {
            if (IsValid(pa_no_compression[i], pa_compression[i])) {
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

    void TestSessionVectorFieldCalc(std::string image_opts, const CARTA::FileType& file_type, int mip, bool fractional,
        bool debiasing = true, double q_error = 0, double u_error = 0, double threshold = 0, int stokes_intensity = 1,
        int stokes_angle = 1) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int width, height;
        std::vector<float> pi, pa;
        CalcPiPa(file_path_string, file_type, channel, mip, debiasing, fractional, threshold, q_error, u_error, width, height, pi, pa);

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
            0, mip, fractional, threshold, debiasing, q_error, u_error, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);
        dummy_backend->Receive(set_vector_field_params);
        dummy_backend->WaitForJobFinished();

        // Set results data
        std::pair<std::vector<char>, bool> message_pair;
        std::vector<float> pi2(width * height), pa2(width * height);
        std::vector<double> progresses;

        while (dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::VECTOR_OVERLAY_TILE_DATA) {
                auto response = Message::DecodeMessage<CARTA::VectorOverlayTileData>(message);
                EXPECT_EQ(response.intensity_tiles_size(), 1);
                if (response.intensity_tiles_size()) {
                    auto tile_pi = response.intensity_tiles(0);
                    GetTileData(tile_pi, width, pi2);
                }
                EXPECT_EQ(response.angle_tiles_size(), 1);
                if (response.angle_tiles_size()) {
                    auto tile_pa = response.angle_tiles(0);
                    GetTileData(tile_pa, width, pa2);
                }
                progresses.push_back(response.progress());
            }
        }

        // Check results
        if (file_type == CARTA::FileType::HDF5) {
            RemoveRightBottomEdgeData(pi, pi2, pa, pa2, width, height);
            CmpVectors(pi, pi2, 1e-5);
            CmpVectors(pa, pa2, 1e-3);
        } else {
            CmpVectors(pi, pi2);
            CmpVectors(pa, pa2);
        }
        CheckProgresses(progresses);
    }

    static void TestImageWithNoStokesAxis(std::string image_shape, std::string image_opts, const CARTA::FileType& file_type, int mip,
        int stokes_intensity, int stokes_angle, double threshold = std::numeric_limits<double>::quiet_NaN()) {
        // Create the sample image
        std::string file_path_string = GenerateImage(file_type, image_opts);

        // =======================================================================================================
        // Calculate the vector field with the whole 2D image data

        int channel = 0;
        int stokes = 0;
        int width, height;
        std::vector<float> pixels;
        GetDownsampledPixels(file_path_string, file_type, channel, stokes, mip, width, height, pixels);

        // Apply a threshold cut
        if (!std::isnan(threshold)) {
            for (auto& pixel : pixels) {
                if (!std::isnan(pixel) && (pixel < threshold)) {
                    pixel = FLOAT_NAN;
                }
            }
        }

        // Check the threshold cut results
        for (auto pixel : pixels) {
            if (IsValid(pixel, threshold)) {
                EXPECT_GE(pixel, threshold);
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
            0, mip, false, threshold, false, 0, 0, stokes_intensity, stokes_angle, CARTA::CompressionType::NONE, 0);
        dummy_backend->Receive(set_vector_field_params);
        dummy_backend->WaitForJobFinished();

        // Set results data
        std::pair<std::vector<char>, bool> message_pair;
        std::vector<float> pi(width * height);
        std::vector<float> pa2(width * height);
        std::vector<double> progresses;

        while (dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::VECTOR_OVERLAY_TILE_DATA) {
                auto response = Message::DecodeMessage<CARTA::VectorOverlayTileData>(message);
                if (stokes_intensity > -1) {
                    EXPECT_EQ(response.intensity_tiles_size(), 1);
                    if (response.intensity_tiles_size()) {
                        auto tile_pi = response.intensity_tiles(0);
                        GetTileData(tile_pi, width, pi);
                    }
                }
                if (stokes_angle > -1) {
                    EXPECT_EQ(response.angle_tiles_size(), 1);
                    if (response.angle_tiles_size()) {
                        auto tile_pa = response.angle_tiles(0);
                        GetTileData(tile_pa, width, pa2);
                    }
                }
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
    std::string stokes_type = "Ix";
    int mip = 4;
    EXPECT_TRUE(TestTilesData(image_opts, file_type, stokes_type, mip));
}

TEST_F(VectorFieldTest, TestBlockSmooth) {
    std::string image_opts = IMAGE_OPTS_NAN;
    CARTA::FileType file_type = CARTA::FileType::FITS;
    std::string stokes_type = "Ix";
    int mip = 4;
    EXPECT_TRUE(TestBlockSmooth(image_opts, file_type, stokes_type, mip));
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
    auto msg = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg1 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg2 = Message::SetVectorOverlayParameters(0, 4, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg3 = Message::SetVectorOverlayParameters(0, 2, false, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg4 = Message::SetVectorOverlayParameters(0, 2, true, 0.2, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg5 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, false, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg6 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.02, 0.02, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg7 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.03, -1, -1, CARTA::CompressionType::ZFP, 1);
    auto msg8 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, 0, -1, CARTA::CompressionType::ZFP, 1);
    auto msg9 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, -1, 0, CARTA::CompressionType::ZFP, 1);
    auto msg10 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::NONE, 1);
    auto msg11 = Message::SetVectorOverlayParameters(0, 2, true, 0.1, true, 0.01, 0.02, -1, -1, CARTA::CompressionType::ZFP, 2);

    VectorFieldSettings settings(msg);
    VectorFieldSettings settings1(msg1);
    VectorFieldSettings settings2(msg2);
    VectorFieldSettings settings3(msg3);
    VectorFieldSettings settings4(msg4);
    VectorFieldSettings settings5(msg5);
    VectorFieldSettings settings6(msg6);
    VectorFieldSettings settings7(msg7);
    VectorFieldSettings settings8(msg8);
    VectorFieldSettings settings9(msg9);
    VectorFieldSettings settings10(msg10);
    VectorFieldSettings settings11(msg11);

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
    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, -1, 0);
    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, 0, -1);
    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, 0, 0);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, -1, 0);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, -1);
    TestImageWithNoStokesAxis("1110 1110 25", IMAGE_OPTS_NAN, file_type, mip, 0, 0);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, -1, 0);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, -1);
    TestImageWithNoStokesAxis("1110 1110", IMAGE_OPTS_NAN, file_type, mip, 0, 0);

    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, -1, 0, threshold);
    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, 0, -1, threshold);
    TestImageWithNoStokesAxis("1110 1110 25 4", IMAGE_OPTS_NAN, file_type, mip, 0, 0, threshold);
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

TEST_F(VectorFieldTest, TestHdf5DownsampledData) {
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

TEST_F(VectorFieldTest, TestLoaderDownsampledData) {
    int image_width = 1110;
    int image_height = 1110;
    std::string image_shape = fmt::format("{} {} 25 4", image_width, image_height);
    std::string image_opts = IMAGE_OPTS; // Note: if a block contains NAN pixels (using IMAGE_OPTS_NAN), its error would be large
    float abs_error = 1e-6;
    int mip = 12;
    std::string stokes_type = "Ix";
    std::vector<int> loader_mips;
    EXPECT_TRUE(TestLoaderDownsampledData(image_shape, image_opts, stokes_type, loader_mips));

    for (auto loader_mip : loader_mips) {
        if (mip % loader_mip == 0) {
            EXPECT_TRUE(TestBlockSmoothDownsampledData(image_shape, image_opts, stokes_type, mip, loader_mip, abs_error));
        }
    }
}
