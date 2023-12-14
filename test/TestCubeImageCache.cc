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
#include "Util/App.h"
#include "Util/Message.h"

using namespace carta;

static const std::string IMAGE_OPTS = "-s 0 -n row column -d 10";
static const double ONE_MILLION = 1.0e6;
static const std::vector<std::string> STOKES_TYPES = {"I", "Q", "U", "V", "Ptotal", "PFtotal", "Plinear", "PFlinear", "Pangle"};
static const std::vector<int> IMAGE_DIMS = {10, 10, 100, 4};

class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z)
        : Frame(session_id, loader, hdu, default_z) {}
    std::vector<float> GetImageDataPerChannel(int z, int stokes) {
        std::vector<float> results;
        auto* data = GetImageData(z, stokes);
        if (data) {
            for (int i = 0; i < Width() * Height(); ++i) {
                results.push_back(data[i]);
            }
        } else {
            GetZMatrix(results, z, stokes);
        }
        return results;
    }
};

class CubeImageCacheTest : public ::testing::Test, public ImageGenerator {
public:
    enum FileType { FITS, HDF5 };
    enum ImageCacheType { Channel, All };

    static std::string GenerateFile(FileType file_type, std::string image_dims) {
        return file_type == FileType::FITS ? GeneratedFitsImagePath(image_dims, IMAGE_OPTS)
                                           : GeneratedHdf5ImagePath(image_dims, IMAGE_OPTS);
    }

    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate().back() == 'x') {
            return {data.profiles(0), data.profiles(1)};
        }
        return {data.profiles(1), data.profiles(0)};
    }

    static void SetFullImageCacheSizeAvailable(
        const ImageCacheType& image_cache_type, int x_size, int y_size, int z_size, int stokes_size) {
        std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
        if (image_cache_type == ImageCacheType::All) {
            FULL_IMAGE_CACHE_SIZE_AVAILABLE = x_size * y_size * z_size * (stokes_size + 6) * sizeof(float) / ONE_MILLION;
        } else {
            FULL_IMAGE_CACHE_SIZE_AVAILABLE = 0;
        }
        ulock.unlock();
    }

    static std::string TextPrefix(const ImageCacheType& image_cache_type) {
        if (image_cache_type == ImageCacheType::All) {
            return "[w/ all image cache]";
        }
        return "[w/ channel image cache]";
    }

    static void SpatialProfile3D(
        const FileType file_type, const std::string& path_string, const std::vector<int>& dims, const ImageCacheType& image_cache_type) {
        if (dims.size() < 3) {
            return;
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, 1);
        int default_channel(0);

        Timer t;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel));
        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for creating a Frame object: {:.3f} ms.\n", file, TextPrefix(image_cache_type), dt.ms());

        std::unique_ptr<DataReader> reader;
        if (file_type == FileType::FITS) {
            reader = std::make_unique<FitsDataReader>(path_string);
        } else {
            reader = std::make_unique<Hdf5DataReader>(path_string);
        }

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
            CmpValues(data.value(), reader->ReadPointXY(x, y, channel));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), x_size);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), x_size);
            CmpVectors(x_vals, reader->ReadProfileX(y, channel));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), y_size);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), y_size);
            CmpVectors(y_vals, reader->ReadProfileY(x, channel));
        }
    }

    static void SpatialProfile4D(const FileType file_type, const std::string& path_string, const std::vector<int>& dims,
        std::string stokes_config, const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return;
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        int default_channel(0);

        Timer t;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel));
        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for creating the Frame object ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type), stokes_config,
            dt.ms());

        std::unique_ptr<DataReader> reader;
        if (file_type == FileType::FITS) {
            reader = std::make_unique<FitsDataReader>(path_string);
        } else {
            reader = std::make_unique<Hdf5DataReader>(path_string);
        }

        int x(4), y(6);
        int channel(5);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config, stokes));

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig(stokes_config + "x"), Message::SpatialConfig(stokes_config + "y")};
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
            EXPECT_EQ(data.stokes(), stokes);
            CmpValues(data.value(), reader->ReadPointXY(x, y, channel, stokes));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), x_size);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), x_size);
            CmpVectors(x_vals, reader->ReadProfileX(y, channel, stokes));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), y_size);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), y_size);
            CmpVectors(y_vals, reader->ReadProfileY(x, channel, stokes));
        }
    }

    static std::vector<CARTA::SpatialProfileData> GetSpatialProfile4D(const FileType file_type, const std::string& path_string,
        const std::vector<int>& dims, std::string stokes_config, const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return std::vector<CARTA::SpatialProfileData>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        int default_channel(0);

        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel));

        std::unique_ptr<DataReader> reader;
        if (file_type == FileType::FITS) {
            reader = std::make_unique<FitsDataReader>(path_string);
        } else {
            reader = std::make_unique<Hdf5DataReader>(path_string);
        }

        int x(4), y(6);
        int channel(5);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config, stokes));

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig(stokes_config + "x"), Message::SpatialConfig(stokes_config + "y")};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(x, y);
        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;

        Timer t;

        frame->FillSpatialProfileData(data_vec);

        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for getting cursor spatial profile ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type),
            stokes_config, dt.ms());

        return data_vec;
    }

    static std::vector<float> CursorSpectralProfile3D(
        const FileType file_type, const std::string& path_string, const std::vector<int>& dims, const ImageCacheType& image_cache_type) {
        if (dims.size() < 3) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, 1);
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", 0));

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
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for getting cursor spectral profile: {:.3f} ms.\n", file, TextPrefix(image_cache_type), dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static std::vector<float> CursorSpectralProfile4D(const FileType file_type, const std::string& path_string,
        const std::vector<int>& dims, std::string stokes_config_z, const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", 0));

        int x(4), y(6);
        int channel(5);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config_z, stokes));

        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed(false);

        Timer t;
        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);
        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for getting cursor spectral profile ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type),
            stokes_config_z, dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static std::vector<float> PointRegionSpectralProfile(const FileType file_type, const std::string& path_string,
        const std::vector<int>& dims, std::string stokes_config_z, const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return std::vector<float>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        auto frame = std::make_shared<Frame>(0, loader, "0", 0);

        int channel(5);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config_z, stokes));

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
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for getting point region spectral profile ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type),
            stokes_config_z, dt.ms());

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static carta::Histogram CubeHistogram(const FileType file_type, const std::string& path_string, const std::vector<int>& dims,
        std::string stokes_config, const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return carta::Histogram();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        auto frame = std::make_shared<Frame>(0, loader, "0", 0);

        int channel(0);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config, stokes));

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
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for calculating cube histogram ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type), stokes_config,
            dt.ms());

        return cube_histogram;
    }

    static std::vector<CARTA::SpectralProfile> RegionSpectralProfile(const FileType file_type, const std::string& path_string,
        const std::vector<int>& dims, const std::string& stokes_config_z, const CARTA::RegionType& region_type,
        const ImageCacheType& image_cache_type) {
        if (dims.size() < 4) {
            return std::vector<CARTA::SpectralProfile>();
        }
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        auto frame = std::make_shared<Frame>(0, loader, "0", 0);

        int channel(5);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config_z, stokes));

        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a rectangle region state: [(cx, cy), (width, height)], or set an ellipse region state: [(cx,cy), (bmaj, bmin)]
        int region_id(1);
        int cx(x_size / 2);
        int cy(y_size / 2);
        int width(x_size / 2);  // or bmaj for ellipse region
        int height(y_size / 2); // or bmin for ellipse region
        std::vector<CARTA::Point> points = {Message::Point(cx, cy), Message::Point(width, height)};

        int file_id(0);
        RegionState region_state(file_id, region_type, points, 0);
        EXPECT_TRUE(region_handler->SetRegion(region_id, region_state, frame->CoordinateSystem()));

        // Set spectral configs for a point region
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

        // Get cursor spectral profile data from the RegionHandler
        std::vector<CARTA::SpectralProfile> spectral_profiles;
        bool stokes_changed(false);

        Timer t;
        region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    for (int i = 0; i < tmp_spectral_profile.profiles_size(); ++i) {
                        spectral_profiles.emplace_back(tmp_spectral_profile.profiles(i));
                    }
                }
            },
            region_id, file_id, stokes_changed);
        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        std::string type = region_type == CARTA::RegionType::RECTANGLE ? "rectangle" : "ellipse";
        fmt::print("{}{} Elapsed time for getting {} region spectral profile ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type), type,
            stokes_config_z, dt.ms());

        return spectral_profiles;
    }

    static bool CmpSpectralProfiles(
        const std::vector<CARTA::SpectralProfile>& spectral_profiles1, const std::vector<CARTA::SpectralProfile>& spectral_profiles2) {
        if (spectral_profiles1.size() != spectral_profiles2.size()) {
            fmt::print("Size of spectral profiles are not equal!\n");
            return false;
        }

        for (int i = 0; i < spectral_profiles1.size(); ++i) {
            auto profile1 = spectral_profiles1[i];
            auto profile2 = spectral_profiles2[i];
            if (profile1.stats_type() != profile2.stats_type()) {
                fmt::print("Statistics type of spectral profiles are not equal!\n");
                return false;
            }

            auto vals1 = GetSpectralProfileValues<double>(profile1);
            auto vals2 = GetSpectralProfileValues<double>(profile2);
            if (vals1.size() != vals2.size()) {
                fmt::print("Data size of spectral profiles are not equal!\n");
                return false;
            }

            EXPECT_GT(vals1.size(), 0);

            for (int j = 0; j < vals1.size(); ++j) {
                if ((!isnan(vals1[j]) && isnan(vals2[j])) || (isnan(vals1[j]) && !isnan(vals2[j]))) {
                    fmt::print("NaN data are not consistent: index = {}!\n", j);
                    return false;
                }
                if (!isnan(vals1[j]) && !isnan(vals2[j])) {
                    auto diff = fabs(vals1[j] - vals2[j]);
                    EXPECT_LT(diff / fabs(vals1[j]), 1.0e-6);
                    EXPECT_LT(diff / fabs(vals2[j]), 1.0e-6);
                }
            }
        }
        return true;
    }

    static std::vector<std::vector<float>> TestImagePixelData(const FileType file_type, const std::string& path_string,
        const std::vector<int>& dims, std::string stokes_config, const ImageCacheType& image_cache_type) {
        int x_size = dims[0];
        int y_size = dims[1];
        int z_size = dims[2];
        int stokes_size = dims[3];

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        SetFullImageCacheSizeAvailable(image_cache_type, x_size, y_size, z_size, stokes_size);
        auto frame = std::make_shared<TestFrame>(0, loader, "0", 0);

        int channel(0);
        int stokes(0);
        EXPECT_TRUE(frame->GetStokesTypeIndex(stokes_config, stokes));

        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        Timer t;

        std::vector<std::vector<float>> results;
        for (int z = 0; z < frame->Depth(); ++z) {
            results.push_back(frame->GetImageDataPerChannel(z, stokes));
        }

        auto dt = t.Elapsed();
        std::string file = file_type == FileType::FITS ? "[FITS]" : "[HDF5]";
        fmt::print("{}{} Elapsed time for getting image pixel data ({}): {:.3f} ms.\n", file, TextPrefix(image_cache_type), stokes_config,
            dt.ms());

        return results;
    }
};

TEST_F(CubeImageCacheTest, SpatialProfile3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));

    SpatialProfile3D(file_type, filename, IMAGE_DIMS, ImageCacheType::Channel);
    SpatialProfile3D(file_type, filename, IMAGE_DIMS, ImageCacheType::All);

    auto results1 = GetSpatialProfile4D(file_type, filename, IMAGE_DIMS, "", ImageCacheType::Channel);
    auto results2 = GetSpatialProfile4D(file_type, filename, IMAGE_DIMS, "", ImageCacheType::All);
    CmpSpatialProfiles(results1, results2);
}

TEST_F(CubeImageCacheTest, SpatialProfile4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));

    std::vector<std::string> normal_stokes = {"I", "Q", "U", "V"};
    for (auto stokes : normal_stokes) {
        SpatialProfile4D(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::Channel);
        SpatialProfile4D(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::All);
    }

    for (auto stokes : STOKES_TYPES) {
        auto results1 = GetSpatialProfile4D(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::Channel);
        auto results2 = GetSpatialProfile4D(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::All);
        CmpSpatialProfiles(results1, results2);
    }
}

TEST_F(CubeImageCacheTest, CursorSpectralProfile3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));

    auto spectral_profile1 = CursorSpectralProfile3D(file_type, filename, IMAGE_DIMS, ImageCacheType::Channel);
    auto spectral_profile2 = CursorSpectralProfile3D(file_type, filename, IMAGE_DIMS, ImageCacheType::All);
    CmpVectors(spectral_profile1, spectral_profile2);
}

TEST_F(CubeImageCacheTest, CursorSpectralProfile4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));

    for (auto stokes : STOKES_TYPES) {
        auto spectral_profile1 = CursorSpectralProfile4D(file_type, filename, IMAGE_DIMS, stokes + "z", ImageCacheType::Channel);
        auto spectral_profile2 = CursorSpectralProfile4D(file_type, filename, IMAGE_DIMS, stokes + "z", ImageCacheType::All);
        CmpVectors(spectral_profile1, spectral_profile2);
    }
}

TEST_F(CubeImageCacheTest, PointRegionSpectralProfile3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));

    auto spectral_profile1 = PointRegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", ImageCacheType::Channel);
    auto spectral_profile2 = PointRegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", ImageCacheType::All);
    CmpVectors(spectral_profile1, spectral_profile2);
}

TEST_F(CubeImageCacheTest, PointRegionSpectralProfile4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));

    for (auto stokes : STOKES_TYPES) {
        auto spectral_profile1 = PointRegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", ImageCacheType::Channel);
        auto spectral_profile2 = PointRegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", ImageCacheType::All);
        CmpVectors(spectral_profile1, spectral_profile2);
    }
}

TEST_F(CubeImageCacheTest, CubeHistogram3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));

    auto hist1 = CubeHistogram(file_type, filename, IMAGE_DIMS, "", ImageCacheType::Channel);
    auto hist2 = CubeHistogram(file_type, filename, IMAGE_DIMS, "", ImageCacheType::All);
    EXPECT_TRUE(CmpHistograms(hist1, hist2));
}

TEST_F(CubeImageCacheTest, CubeHistogram4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));

    std::vector<std::string> normal_stokes = {"I", "Q", "U", "V"};
    std::vector<std::string> computed_stokes = {"Ptotal", "PFtotal", "Plinear", "PFlinear", "Pangle"};

    for (auto stokes : normal_stokes) {
        auto hist1 = CubeHistogram(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::Channel);
        auto hist2 = CubeHistogram(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::All);
        EXPECT_TRUE(CmpHistograms(hist1, hist2));
    }

    for (auto stokes : computed_stokes) {
        auto hist1 = CubeHistogram(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::Channel);
        auto hist2 = CubeHistogram(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::All);
        EXPECT_TRUE(CmpHistograms(hist1, hist2, false));
    }
}

TEST_F(CubeImageCacheTest, RectangleRegionSpectralProfile3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;

    auto spectral_profile1 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", region_type, ImageCacheType::Channel);
    auto spectral_profile2 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", region_type, ImageCacheType::All);
    EXPECT_TRUE(CmpSpectralProfiles(spectral_profile1, spectral_profile2));
}

TEST_F(CubeImageCacheTest, RectangleRegionSpectralProfile4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;

    for (auto stokes : STOKES_TYPES) {
        auto spectral_profile1 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", region_type, ImageCacheType::Channel);
        auto spectral_profile2 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", region_type, ImageCacheType::All);
        EXPECT_TRUE(CmpSpectralProfiles(spectral_profile1, spectral_profile2));
    }
}

TEST_F(CubeImageCacheTest, EllipseRegionSpectralProfile3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;

    auto spectral_profile1 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", region_type, ImageCacheType::Channel);
    auto spectral_profile2 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, "z", region_type, ImageCacheType::All);
    EXPECT_TRUE(CmpSpectralProfiles(spectral_profile1, spectral_profile2));
}

TEST_F(CubeImageCacheTest, EllipseRegionSpectralProfile4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;

    for (auto stokes : STOKES_TYPES) {
        auto spectral_profile1 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", region_type, ImageCacheType::Channel);
        auto spectral_profile2 = RegionSpectralProfile(file_type, filename, IMAGE_DIMS, stokes + "z", region_type, ImageCacheType::All);
        EXPECT_TRUE(CmpSpectralProfiles(spectral_profile1, spectral_profile2));
    }
}

TEST_F(CubeImageCacheTest, ImagePixelData3D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2]));

    auto data1 = TestImagePixelData(file_type, filename, IMAGE_DIMS, "", ImageCacheType::Channel);
    auto data2 = TestImagePixelData(file_type, filename, IMAGE_DIMS, "", ImageCacheType::All);
    for (int i = 0; i < data1.size(); ++i) {
        CmpVectors(data1[i], data2[i]);
    }
}

TEST_F(CubeImageCacheTest, ImagePixelData4D) {
    FileType file_type = FileType::FITS;
    std::string filename = GenerateFile(file_type, fmt::format("{} {} {} {}", IMAGE_DIMS[0], IMAGE_DIMS[1], IMAGE_DIMS[2], IMAGE_DIMS[3]));

    for (auto stokes : STOKES_TYPES) {
        auto data1 = TestImagePixelData(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::Channel);
        auto data2 = TestImagePixelData(file_type, filename, IMAGE_DIMS, stokes, ImageCacheType::All);
        for (int i = 0; i < data1.size(); ++i) {
            CmpVectors(data1[i], data2[i]);
        }
    }
}
