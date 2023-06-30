/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "Region/RegionHandler.h"
#include "Timer/Timer.h"
#include "Util/Message.h"

using namespace carta;

static const std::string IMAGE_OPTS = "-s 0 -n row column -d 10";
static const double ONE_MILLION = 1000000;

class CubeImageCacheTest : public ::testing::Test, public ImageGenerator {
public:
    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate().back() == 'x') {
            return {data.profiles(0), data.profiles(1)};
        }
        return {data.profiles(1), data.profiles(0)};
    }

    static void Fits3DSpatialProfile(const std::vector<int>& dims, int default_channel, bool cube_image_cache) {
        if (dims.size() != 3) {
            return;
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        std::string image_dims = fmt::format("{} {} {}", x_size, y_size, z_size);
        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * sizeof(float) / ONE_MILLION) : 0;

        Timer t;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));
        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on creating the Frame object: {:.3f} ms.\n", dt.ms());

        FitsDataReader reader(path_string);

        int x(1), y(1);
        int channel(5);
        int stokes(0);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        frame->SetSpatialRequirements(profiles);
        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), x);
            EXPECT_EQ(data.y(), y);
            EXPECT_EQ(data.channel(), channel);
            EXPECT_EQ(data.stokes(), stokes);
            CmpValues(data.value(), reader.ReadPointXY(x, y, channel));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), x_size);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), x_size);
            CmpVectors(x_vals, reader.ReadProfileX(y, channel));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), y_size);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), y_size);
            CmpVectors(y_vals, reader.ReadProfileY(x, channel));
        }
    }

    static void Fits4DSpatialProfile(const std::vector<int>& dims, int default_channel, bool cube_image_cache) {
        if (dims.size() != 4) {
            return;
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];
        std::string image_dims = fmt::format("{} {} {} {}", x_size, y_size, z_size, stokes_size);

        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * stokes_size * sizeof(float) / ONE_MILLION) : 0;

        Timer t;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));
        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on creating the Frame object: {:.3f} ms.\n", dt.ms());

        FitsDataReader reader(path_string);

        int x(4), y(6);
        int channel(5);
        int stokes(2);
        int spatial_config_stokes(1); // set spatial config coordinate = {"Qx", "Qy"}

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("Qx"), Message::SpatialConfig("Qy")};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(x, y);
        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), x);
            EXPECT_EQ(data.y(), y);
            EXPECT_EQ(data.channel(), channel);
            EXPECT_EQ(data.stokes(), spatial_config_stokes);
            CmpValues(data.value(), reader.ReadPointXY(x, y, channel, spatial_config_stokes));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), x_size);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), x_size);
            CmpVectors(x_vals, reader.ReadProfileX(y, channel, spatial_config_stokes));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), y_size);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), y_size);
            CmpVectors(y_vals, reader.ReadProfileY(x, channel, spatial_config_stokes));
        }
    }

    static std::vector<float> Fits3DCursorSpectralProfile(const std::vector<int>& dims, bool cube_image_cache) {
        if (dims.size() != 3) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        std::string image_dims = fmt::format("{} {} {}", x_size, y_size, z_size);
        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", 0, reserved_memory));

        int x(4), y(6);
        int channel(5);
        int stokes(0);
        std::string stokes_config_z("z");

        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        Timer t;
        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);
        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on getting cursor spectral profile: {:.3f} ms.\n", dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static std::vector<float> Fits4DCursorSpectralProfile(
        const std::vector<int>& dims, std::string stokes_config_z, bool cube_image_cache) {
        if (dims.size() != 4) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];
        std::string image_dims = fmt::format("{} {} {} {}", x_size, y_size, z_size, stokes_size);
        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * stokes_size * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", 0, reserved_memory));

        int x(4), y(6);
        int channel(5);
        int stokes(0);

        if (stokes_config_z.size() == 2 && stokes_config_z.back() == 'z') {
            char pol = stokes_config_z.front();
            if (pol == 'Q') {
                stokes = 1;
            } else if (pol == 'U') {
                stokes = 2;
            } else if (pol == 'V') {
                stokes = 3;
            }
        }

        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        Timer t;
        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);
        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on getting cursor spectral profile: {:.3f} ms.\n", dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static std::vector<float> PointRegionSpectralProfile(const std::vector<int>& dims, std::string stokes_config_z, bool cube_image_cache) {
        if (dims.size() != 4) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];
        std::string image_dims = fmt::format("{} {} {} {}", x_size, y_size, z_size, stokes_size);
        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * stokes_size * sizeof(float) / ONE_MILLION) : 0;
        auto frame = std::make_shared<Frame>(0, loader, "0", 0, reserved_memory);

        int channel(5);
        int stokes(0);

        if (stokes_config_z.size() == 2 && stokes_config_z.back() == 'z') {
            char pol = stokes_config_z.front();
            if (pol == 'Q') {
                stokes = 1;
            } else if (pol == 'U') {
                stokes = 2;
            } else if (pol == 'V') {
                stokes = 3;
            }
        }

        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a point region state
        int region_id(1);
        int point_x = x_size / 2;
        int point_y = y_size / 2;
        std::vector<CARTA::Point> points = {Message::Point(point_x, point_y)};

        int file_id(0);
        RegionState region_state(file_id, CARTA::RegionType::POINT, points, 0);
        EXPECT_TRUE(region_handler->SetRegion(region_id, region_state, frame->CoordinateSystem()));

        // Set spectral configs for a point region
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

        // Get cursor spectral profile data from the RegionHandler
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed(false);

        Timer t;
        region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            region_id, file_id, stokes_changed);
        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on getting point region spectral profile: {:.3f} ms.\n", dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static carta::Histogram CubeHistogram(const std::vector<int>& dims, std::string stokes_config_z, bool cube_image_cache) {
        if (dims.size() != 4) {
            return carta::Histogram();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];
        std::string image_dims = fmt::format("{} {} {} {}", x_size, y_size, z_size, stokes_size);
        auto path_string = GeneratedFitsImagePath(image_dims, IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(x_size * y_size * z_size * stokes_size * sizeof(float) / ONE_MILLION) : 0;
        auto frame = std::make_shared<Frame>(0, loader, "0", 0, reserved_memory);

        int channel(0);
        int stokes(0);

        if (stokes_config_z == "I") {
            stokes = 0;
        } else if (stokes_config_z == "Q") {
            stokes = 1;
        } else if (stokes_config_z == "U") {
            stokes = 2;
        } else if (stokes_config_z == "V") {
            stokes = 3;
        } else if (stokes_config_z == "Ptotal") {
            stokes = COMPUTE_STOKES_PTOTAL;
        } else if (stokes_config_z == "PFtotal") {
            stokes = COMPUTE_STOKES_PFTOTAL;
        } else if (stokes_config_z == "Plinear") {
            stokes = COMPUTE_STOKES_PLINEAR;
        } else if (stokes_config_z == "PFlinear") {
            stokes = COMPUTE_STOKES_PFLINEAR;
        } else if (stokes_config_z == "Pangle") {
            stokes = COMPUTE_STOKES_PANGLE;
        }

        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        Timer t;

        // stats for entire cube
        size_t depth(frame->Depth());
        carta::BasicStats<float> cube_stats;
        for (size_t z = 0; z < depth; ++z) {
            carta::BasicStats<float> z_stats;
            if (!frame->GetBasicStats(z, stokes, z_stats)) {
                std::cerr << "Failed to get statistics data form the cube histogram calculation!\n";
                return carta::Histogram();
            }
            cube_stats.join(z_stats);
        }

        auto bounds = HistogramBounds(cube_stats.min_val, cube_stats.max_val);

        // get histogram bins for each z and accumulate bin counts in cube_bins
        carta::Histogram cube_histogram;
        carta::Histogram z_histogram;
        for (size_t z = 0; z < depth; ++z) {
            if (!frame->CalculateHistogram(CUBE_REGION_ID, z, stokes, -1, bounds, z_histogram)) {
                std::cerr << "Failed to calculate the cube histogram!\n";
                return carta::Histogram();
            }
            if (z == 0) {
                cube_histogram = std::move(z_histogram);
            } else {
                cube_histogram.Add(z_histogram);
            }
        }

        auto dt = t.Elapsed();
        std::cout << fmt::format("Time spend on calculating cube histogram: {:.3f} ms.\n", dt.ms());

        return cube_histogram;
    }
};

TEST_F(CubeImageCacheTest, Fits3DSpatialProfile) {
    std::vector<int> image_dims = {100, 100, 100};
    Fits3DSpatialProfile(image_dims, 0, false);
    Fits3DSpatialProfile(image_dims, 0, true);
    Fits3DSpatialProfile(image_dims, 1, false);
    Fits3DSpatialProfile(image_dims, 1, true);
}

TEST_F(CubeImageCacheTest, Fits4DSpatialProfile) {
    std::vector<int> image_dims = {100, 100, 100, 4};
    Fits4DSpatialProfile(image_dims, 0, false);
    Fits4DSpatialProfile(image_dims, 0, true);
    Fits4DSpatialProfile(image_dims, 1, false);
    Fits4DSpatialProfile(image_dims, 1, true);
}

TEST_F(CubeImageCacheTest, Fits3DCursorSpectralProfile) {
    std::vector<int> image_dims = {10, 10, 1000};
    auto spectral_profile1 = Fits3DCursorSpectralProfile(image_dims, false);
    auto spectral_profile2 = Fits3DCursorSpectralProfile(image_dims, true);
    CmpVectors(spectral_profile1, spectral_profile2);
}

TEST_F(CubeImageCacheTest, Fits4DCursorSpectralProfile) {
    std::vector<int> image_dims = {10, 10, 1000, 4};
    auto spectral_profile1 = Fits4DCursorSpectralProfile(image_dims, "Iz", false);
    auto spectral_profile2 = Fits4DCursorSpectralProfile(image_dims, "Iz", true);
    CmpVectors(spectral_profile1, spectral_profile2);

    auto spectral_profile3 = Fits4DCursorSpectralProfile(image_dims, "Qz", false);
    auto spectral_profile4 = Fits4DCursorSpectralProfile(image_dims, "Qz", true);
    CmpVectors(spectral_profile3, spectral_profile4);

    auto spectral_profile5 = Fits4DCursorSpectralProfile(image_dims, "Uz", false);
    auto spectral_profile6 = Fits4DCursorSpectralProfile(image_dims, "Uz", true);
    CmpVectors(spectral_profile5, spectral_profile6);

    auto spectral_profile7 = Fits4DCursorSpectralProfile(image_dims, "Uz", false);
    auto spectral_profile8 = Fits4DCursorSpectralProfile(image_dims, "Uz", true);
    CmpVectors(spectral_profile7, spectral_profile8);

    auto spectral_profile9 = Fits4DCursorSpectralProfile(image_dims, "Ptotalz", false);
    auto spectral_profile10 = Fits4DCursorSpectralProfile(image_dims, "Ptotalz", true);
    CmpVectors(spectral_profile9, spectral_profile10);

    auto spectral_profile11 = Fits4DCursorSpectralProfile(image_dims, "PFtotalz", false);
    auto spectral_profile12 = Fits4DCursorSpectralProfile(image_dims, "PFtotalz", true);
    CmpVectors(spectral_profile11, spectral_profile12);

    auto spectral_profile13 = Fits4DCursorSpectralProfile(image_dims, "Plinearz", false);
    auto spectral_profile14 = Fits4DCursorSpectralProfile(image_dims, "Plinearz", true);
    CmpVectors(spectral_profile13, spectral_profile14);

    auto spectral_profile15 = Fits4DCursorSpectralProfile(image_dims, "PFlinearz", false);
    auto spectral_profile16 = Fits4DCursorSpectralProfile(image_dims, "PFlinearz", true);
    CmpVectors(spectral_profile15, spectral_profile16);

    auto spectral_profile17 = Fits4DCursorSpectralProfile(image_dims, "Panglez", false);
    auto spectral_profile18 = Fits4DCursorSpectralProfile(image_dims, "Panglez", true);
    CmpVectors(spectral_profile17, spectral_profile18);
}

TEST_F(CubeImageCacheTest, PointRegionSpectralProfile) {
    std::vector<int> image_dims = {10, 10, 1000, 4};
    auto spectral_profile1 = PointRegionSpectralProfile(image_dims, "Iz", false);
    auto spectral_profile2 = PointRegionSpectralProfile(image_dims, "Iz", true);
    CmpVectors(spectral_profile1, spectral_profile2);

    auto spectral_profile3 = PointRegionSpectralProfile(image_dims, "Qz", false);
    auto spectral_profile4 = PointRegionSpectralProfile(image_dims, "Qz", true);
    CmpVectors(spectral_profile3, spectral_profile4);

    auto spectral_profile5 = PointRegionSpectralProfile(image_dims, "Uz", false);
    auto spectral_profile6 = PointRegionSpectralProfile(image_dims, "Uz", true);
    CmpVectors(spectral_profile5, spectral_profile6);

    auto spectral_profile7 = PointRegionSpectralProfile(image_dims, "Uz", false);
    auto spectral_profile8 = PointRegionSpectralProfile(image_dims, "Uz", true);
    CmpVectors(spectral_profile7, spectral_profile8);

    auto spectral_profile9 = PointRegionSpectralProfile(image_dims, "Ptotalz", false);
    auto spectral_profile10 = PointRegionSpectralProfile(image_dims, "Ptotalz", true);
    CmpVectors(spectral_profile9, spectral_profile10);

    auto spectral_profile11 = PointRegionSpectralProfile(image_dims, "PFtotalz", false);
    auto spectral_profile12 = PointRegionSpectralProfile(image_dims, "PFtotalz", true);
    CmpVectors(spectral_profile11, spectral_profile12);

    auto spectral_profile13 = PointRegionSpectralProfile(image_dims, "Plinearz", false);
    auto spectral_profile14 = PointRegionSpectralProfile(image_dims, "Plinearz", true);
    CmpVectors(spectral_profile13, spectral_profile14);

    auto spectral_profile15 = PointRegionSpectralProfile(image_dims, "PFlinearz", false);
    auto spectral_profile16 = PointRegionSpectralProfile(image_dims, "PFlinearz", true);
    CmpVectors(spectral_profile15, spectral_profile16);

    auto spectral_profile17 = PointRegionSpectralProfile(image_dims, "Panglez", false);
    auto spectral_profile18 = PointRegionSpectralProfile(image_dims, "Panglez", true);
    CmpVectors(spectral_profile17, spectral_profile18);
}

TEST_F(CubeImageCacheTest, CubeHistogram) {
    std::vector<int> image_dims = {100, 100, 1000, 4};
    auto hist1 = CubeHistogram(image_dims, "I", false);
    auto hist2 = CubeHistogram(image_dims, "I", true);
    EXPECT_TRUE(CmpHistograms(hist1, hist2));

    auto hist3 = CubeHistogram(image_dims, "Q", false);
    auto hist4 = CubeHistogram(image_dims, "Q", true);
    EXPECT_TRUE(CmpHistograms(hist3, hist4));

    auto hist5 = CubeHistogram(image_dims, "U", false);
    auto hist6 = CubeHistogram(image_dims, "U", true);
    EXPECT_TRUE(CmpHistograms(hist5, hist6));

    auto hist7 = CubeHistogram(image_dims, "V", false);
    auto hist8 = CubeHistogram(image_dims, "V", true);
    EXPECT_TRUE(CmpHistograms(hist7, hist8));

    auto hist9 = CubeHistogram(image_dims, "Ptotal", false);
    auto hist10 = CubeHistogram(image_dims, "Ptotal", true);
    EXPECT_TRUE(CmpHistograms(hist9, hist10));

    auto hist11 = CubeHistogram(image_dims, "PFtotal", false);
    auto hist12 = CubeHistogram(image_dims, "PFtotal", true);
    EXPECT_TRUE(CmpHistograms(hist11, hist12));

    auto hist13 = CubeHistogram(image_dims, "Plinear", false);
    auto hist14 = CubeHistogram(image_dims, "Plinear", true);
    EXPECT_TRUE(CmpHistograms(hist13, hist14));

    auto hist15 = CubeHistogram(image_dims, "PFlinear", false);
    auto hist16 = CubeHistogram(image_dims, "PFlinear", true);
    EXPECT_TRUE(CmpHistograms(hist15, hist16));

    auto hist17 = CubeHistogram(image_dims, "Pangle", false);
    auto hist18 = CubeHistogram(image_dims, "Pangle", true);
    EXPECT_TRUE(CmpHistograms(hist17, hist18));
}
