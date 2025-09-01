/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

static const std::string IMAGE_OPTS = "-s 0 -n row column -d 10";

class CursorSpatialProfileTest : public ::testing::Test, public ImageGenerator {
public:
    // Helper function to extract and order X/Y spatial profiles from a SpatialProfileData object.
    //
    // Behavior:
    //   * Takes a CARTA::SpatialProfileData containing two profiles.
    //   * Checks the last character of the coordinate string of the first profile.
    //   * If the coordinate ends with 'x', returns the profiles in the original order (X, Y).
    //   * Otherwise, returns them swapped (Y, X).
    //
    // This function ensures consistent ordering of spatial profiles for downstream
    // analysis, so that the X-axis profile is always returned first and the
    // Y-axis profile second, regardless of how the data is originally structured.
    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate().back() == 'x') {
            return {data.profiles(0), data.profiles(1)};
        } else {
            return {data.profiles(1), data.profiles(0)};
        }
    }

    // Helper function to extract numerical values from a CARTA::SpatialProfile.
    //
    // Behavior:
    //   * Retrieves the raw 32-bit floating-point buffer from the profile.
    //   * Allocates a vector of floats large enough to hold all values.
    //   * Copies the raw bytes into the float vector using memcpy.
    //   * Returns a std::vector<float> containing all profile values.
    //
    // This function is useful for tests or analysis that need to operate on
    // the actual numeric data from a SpatialProfile, rather than the serialized
    // string representation.
    static std::vector<float> ProfileValues(CARTA::SpatialProfile& profile) {
        std::string buffer = profile.raw_values_fp32();
        std::vector<float> values(buffer.size() / sizeof(float));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    // Helper function to decimate a 1D float profile for multiresolution analysis.
    //
    // Arguments:
    //   - full_resolution: the original full-resolution profile.
    //   - mip: the decimation factor.
    //
    // Behavior:
    //   * Divides the input profile into contiguous segments of size (mip * 2).
    //   * For each segment:
    //       - Removes NaN values.
    //       - If non-empty, stores the first occurrence of the minimum and the
    //         last occurrence of the maximum in the output vector.
    //       - If empty, stores NaN for both positions.
    //   * Returns a decimated vector of size num_segments * 2.
    //
    // This function preserves the min/max structure of the profile while reducing
    // its resolution, which is useful for efficient display or analysis of
    // large spatial profiles.
    static std::vector<float> Decimated(std::vector<float> full_resolution, int mip) {
        // Decimate profile in 1D
        size_t num_decimated_pairs = std::ceil((float)full_resolution.size() / (mip * 2));
        std::vector<float> result(num_decimated_pairs * 2);
        for (size_t i = 0; i < num_decimated_pairs; i++) {
            std::vector<float> segment(
                full_resolution.begin() + i * mip * 2, std::min(full_resolution.begin() + (i + 1) * mip * 2, full_resolution.end()));
            // Remove NaN elements
            segment.erase(
                std::remove_if(segment.begin(), segment.end(), [](const auto& value) { return std::isnan(value); }), segment.end());

            if (!segment.empty()) {
                // First occurrence of smallest element
                auto minpos = std::min_element(segment.begin(), segment.end());
                // Last occurrence of largest element (because the real code uses min_max_element)
                auto maxpos = (std::max_element(segment.rbegin(), segment.rend()) + 1).base();
                result[i * 2] = (minpos < maxpos) ? *minpos : *maxpos;
                result[i * 2 + 1] = (minpos < maxpos) ? *maxpos : *minpos;
            } else {
                result[i * 2] = result[i * 2 + 1] = std::numeric_limits<float>::quiet_NaN();
            }
        }
        return result;
    }

    // Helper function to downsample a 2D set of profile vectors into a single 1D profile.
    //
    // Arguments:
    //   - profiles: a vector of profile vectors (2D float array), where each inner
    //     vector represents an adjacent segment of the profile.
    //
    // Behavior:
    //   * Determines the downsampling factor (mip) automatically as the number of
    //     adjacent profiles provided.
    //   * Computes the number of downsampled bins along the width of the profile.
    //   * Iterates over each bin, summing finite values across both the width and
    //     the adjacent profiles, and counts the number of valid contributions.
    //   * Averages the sum for each bin if at least one valid value exists; otherwise,
    //     assigns NaN.
    //   * Returns the downsampled 1D profile vector.
    //
    // This function is useful for generating lower-resolution representations of
    // multiple adjacent profiles while preserving the average structure and
    // handling NaN values gracefully.
    static std::vector<float> Downsampled(std::vector<std::vector<float>> profiles) {
        // Downsample profile in 2D; autodetect mip from number of adjacent profiles provided
        int mip = profiles.size();
        size_t width = profiles[0].size();
        size_t num_downsampled = std::ceil((float)width / mip);
        std::vector<float> result(num_downsampled, 0);
        std::vector<float> count(num_downsampled, 0);

        for (size_t b = 0; b < num_downsampled; b++) {
            for (size_t i = b * mip; i < std::min((b + 1) * mip, width); i++) {
                for (size_t j = 0; j < mip; j++) {
                    if (!std::isnan(profiles[j][i])) {
                        result[b] += profiles[j][i];
                        count[b]++;
                    }
                }
            }
            if (count[b] > 0) {
                result[b] /= count[b];
            } else {
                result[b] = std::numeric_limits<float>::quiet_NaN();
            }
        }

        return result;
    }

    // Helper function to extract a contiguous segment from a 1D profile.
    //
    // Arguments:
    //   - profile: the full-resolution 1D profile vector.
    //   - start: the starting index (inclusive) of the segment.
    //   - end: the ending index (exclusive) of the segment.
    //
    // Behavior:
    //   * Copies elements from profile[start] up to, but not including, profile[end]
    //     into a new std::vector<float>.
    //   * Returns the resulting sub-segment vector.
    //
    // This function is useful for slicing portions of a profile for analysis or
    // testing purposes, without modifying the original profile.
    static std::vector<float> Segment(std::vector<float> profile, size_t start, size_t end) {
        std::vector<float> result;
        std::copy(profile.begin() + start, profile.begin() + end, std::back_inserter(result));
        return result;
    }

    std::vector<CARTA::SpatialProfileData> LoadProfiles(
        const std::string& path, 
        const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& profiles,
        int x, int y
    ) {
        std::shared_ptr<carta::FileLoader> loader = std::shared_ptr<carta::FileLoader>(carta::FileLoader::GetLoader(path));
        std::unique_ptr<Frame> frame = std::make_unique<Frame>(0, loader, "0");
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(x, y);
        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);
        return data_vec;
    }

    void SetUp() {
        setenv("HDF5_USE_FILE_LOCKING", "FALSE", 0);
    }
};

struct ExpectedProfile {
    int file_id;
    int x;
    int y;
    int channel;
    int stokes;
    int profiles_size;
    std::pair<int,int> x_range;
    int x_mip;
    size_t x_size;
    std::pair<int,int> y_range;
    int y_mip;
    size_t y_size;
};

enum class ReaderType { Fits, Hdf5 };

struct CorrectProfileTestParams {
    std::string imageFile;
    std::vector<int> cursorDims;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    std::pair<int,int> cmpValues; // first: x, second: y
    ExpectedProfile expected;
    ReaderType readerType;
};

class CorrectProfileTest :
    public CursorSpatialProfileTest, public ::testing::TestWithParam<CorrectProfileTestParams> {};


TEST_P(CorrectProfileTest, GeneratesCorrectProfile) {
    auto params = GetParam();
    std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(params.imageFile, params.profiles, params.cursorDims[0], params.cursorDims[1]);
    
    std::unique_ptr<DataReader> reader;
    if (params.readerType == ReaderType::Fits) {
        reader = std::make_unique<FitsDataReader>(params.imageFile);
    } else {
        reader = std::make_unique<Hdf5DataReader>(params.imageFile);
    }

    for (auto& data : data_vec) {
        EXPECT_EQ(data.file_id(), params.expected.file_id);
        EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
        EXPECT_EQ(data.x(), params.expected.x);
        EXPECT_EQ(data.y(), params.expected.y);
        EXPECT_EQ(data.channel(), params.expected.channel);
        EXPECT_EQ(data.stokes(), params.expected.stokes);
        CmpValues(data.value(), reader->ReadPointXY(params.cmpValues.first, params.cmpValues.second));
        ASSERT_EQ(data.profiles_size(), params.expected.profiles_size);
        EXPECT_EQ(data.profiles_size(), params.expected.profiles_size);

        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), params.expected.x_range.first);
        EXPECT_EQ(x_profile.end(), params.expected.x_range.second);
        EXPECT_EQ(x_profile.mip(), params.expected.x_mip);
        auto x_vals = ProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), params.expected.x_size);
        CmpVectors<float>(x_vals, reader->ReadProfileX(params.cmpValues.first));

        EXPECT_EQ(y_profile.start(), params.expected.y_range.first);
        EXPECT_EQ(y_profile.end(), params.expected.y_range.second);
        EXPECT_EQ(y_profile.mip(), params.expected);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), params.expected.y_size);
        CmpVectors<float>(y_vals, reader->ReadProfileY(params.cmpValues.second));
    }
}

INSTANTIATE_TEST_SUITE_P(
    CorrectProfiles,
    CorrectProfileTest,
    ::testing::Values(
        CorrectProfileTestParams{ // small fits profile
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits",
            {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, 
            {5, 5},
            {.file_id = 0, .x = 5, .y = 5, .channel = 0, .stokes = 0, .profiles_size = 2,  .x_range = {0, 10}, .x_mip = 0, .x_size = 10, .y_range = {0, 10}, .y_mip = 0, .y_size = 10},
            ReaderType::Fits
        },
        CorrectProfileTestParams{ // low resolution fits profile
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits",
            {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)},
            {5, 5},
            {.file_id = 0, .x = 5, .y = 5, .channel = 0, .stokes = 0, .profiles_size = 2,  .x_range = {0, 10}, .x_mip = 0, .x_size = 10, .y_range = {0, 10}, .y_mip = 0, .y_size = 10},
            ReaderType::Fits
        },
        CorrectProfileTestParams{ // small hdf5 profile
            TestRoot() / "data" / "images" / "hdf5" / "10_10_row_column.hdf5",
            {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")},
            {5, 5},
            {.file_id = 0, .x = 5, .y = 5, .channel = 0, .stokes = 0, .profiles_size = 2,  .x_range = {0, 10}, .x_mip = 0, .x_size = 10, .y_range = {0, 10}, .y_mip = 0, .y_size = 10},
            ReaderType::Hdf5
        }
    )
);

TEST_P(LowResTest, GeneratesCorrectProfile) {
    auto params = GetParam();
    auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits");
    Hdf5DataReader reader(path_string);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
        Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
    std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(params.imageFile, params.profiles, params.cursorDims[0], params.cursorDims[1]);
    
    for (auto& data : data_vec) {
        EXPECT_EQ(data.profiles_size(), params.expected.profiles_size);

        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), params.expected.x_range.first);
        EXPECT_EQ(x_profile.end(), params.expected.x_range.second);
        EXPECT_EQ(x_profile.mip(), params.expected.x_mip);
        auto x_vals = ProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), params.expected.x_size);
        CmpVectors<float>(x_vals, Decimated(reader.ReadProfileX(params.cmpValues.first), params.cmpValues.second));

        EXPECT_EQ(y_profile.start(), 0);
        EXPECT_EQ(y_profile.end(), 100);
        EXPECT_EQ(y_profile.mip(), 2);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), 50);
        CmpVectors<float>(y_vals, Decimated(reader.ReadProfileX(50), 2));
    }
}

    TEST_F(CursorSpatialProfileTest, LowResFitsProfile) {
        auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits");
        Hdf5DataReader reader(path_string);
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 50, 50);
        
        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 130);
            EXPECT_EQ(x_profile.mip(), 2);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 66);
            CmpVectors<float>(x_vals, Decimated(reader.ReadProfileX(50), 2));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 100);
            EXPECT_EQ(y_profile.mip(), 2);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 50);
            CmpVectors<float>(y_vals, Decimated(reader.ReadProfileX(50), 2));
        }
    }

    TEST_F(CursorSpatialProfileTest, LowResHdf5ProfileExactMipAvailable) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "130_100_row_column.hdf5");
        Hdf5DataReader reader(path_string);
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 50, 50);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 130);
            EXPECT_EQ(x_profile.mip(), 2);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 65);
            CmpVectors<float>(x_vals, Downsampled({reader.ReadProfileX(50), reader.ReadProfileX(51)}), 1e-5);

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 100);
            EXPECT_EQ(y_profile.mip(), 2);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 50);
            CmpVectors<float>(y_vals, Downsampled({reader.ReadProfileY(50), reader.ReadProfileY(51)}), 1e-5);
        }
    }

    TEST_F(CursorSpatialProfileTest, LowResHdf5ProfileLowerMipAvailable) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "130_100_row_column.hdf5");
        Hdf5DataReader reader(path_string);
        // mip 4 is requested, but the file only has a dataset for mip 2
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 4), Message::SpatialConfig("y", 0, 0, 4)};
        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 50, 50);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            // the returned profiles should be mip 2
            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 130);
            EXPECT_EQ(x_profile.mip(), 2);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 65);
            CmpVectors<float>(x_vals, Downsampled({reader.ReadProfileX(50), reader.ReadProfileX(51)}), 1e-5);

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 100);
            EXPECT_EQ(y_profile.mip(), 2);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 50);
            CmpVectors<float>(y_vals, Downsampled({reader.ReadProfileY(50), reader.ReadProfileY(51)}), 1e-5);
        }
    }

    TEST_F(CursorSpatialProfileTest, LowResHdf5ProfileNoMipAvailable) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "120_100_row_column.hdf5");
        // mip 2 is requested, but this file is too small to have mipmaps
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 50, 50);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            // the returned profiles should be decimated, as for a FITS file
            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 120);
            EXPECT_EQ(x_profile.mip(), 2);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 60);
            CmpVectors<float>(x_vals, Decimated(reader.ReadProfileX(50), 2));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 100);
            EXPECT_EQ(y_profile.mip(), 2);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 50);
            CmpVectors<float>(y_vals, Decimated(reader.ReadProfileY(50), 2));
        }
    }

    TEST_F(CursorSpatialProfileTest, FullResFitsStartEnd) {
        auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)};
        FitsDataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 150, 150);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 100);
            EXPECT_EQ(x_profile.end(), 200);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 100);
            CmpVectors<float>(x_vals, Segment(reader.ReadProfileX(150), 100, 200));

            EXPECT_EQ(y_profile.start(), 100);
            EXPECT_EQ(y_profile.end(), 200);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 100);
            CmpVectors<float>(y_vals, Segment(reader.ReadProfileY(150), 100, 200));
        }
    }

    TEST_F(CursorSpatialProfileTest, FullResHdf5StartEnd) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "400_300_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)};
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 150, 150);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 100);
            EXPECT_EQ(x_profile.end(), 200);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 100);
            CmpVectors<float>(x_vals, Segment(reader.ReadProfileX(150), 100, 200));

            EXPECT_EQ(y_profile.start(), 100);
            EXPECT_EQ(y_profile.end(), 200);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 100);
            CmpVectors<float>(y_vals, Segment(reader.ReadProfileY(150), 100, 200));
        }
    }

    TEST_F(CursorSpatialProfileTest, LowResFitsStartEnd) {
        auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)};
        FitsDataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 150, 150);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 100);
            EXPECT_EQ(x_profile.end(), 200);
            EXPECT_EQ(x_profile.mip(), 4);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 24);
            // Data to decimate has endpoints rounded up to mip*2
            CmpVectors<float>(x_vals, Decimated(Segment(reader.ReadProfileX(150), 104, 200), 4));

            EXPECT_EQ(y_profile.start(), 100);
            EXPECT_EQ(y_profile.end(), 200);
            EXPECT_EQ(y_profile.mip(), 4);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 24);
            // Data to decimate has endpoints rounded up to mip*2
            CmpVectors<float>(y_vals, Decimated(Segment(reader.ReadProfileY(150), 104, 200), 4));
        }
    }

    TEST_F(CursorSpatialProfileTest, LowResHdf5StartEnd) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "400_300_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)};
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 150, 150);    

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 100);
            EXPECT_EQ(x_profile.end(), 200);
            EXPECT_EQ(x_profile.mip(), 4);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 25);
            // Downsampled region is selected so that it includes the requested row
            CmpVectors<float>(x_vals,
                Segment(
                    Downsampled({reader.ReadProfileX(148), reader.ReadProfileX(149), reader.ReadProfileX(150), reader.ReadProfileX(151)}),
                    25, 50),
                1e-5);

            EXPECT_EQ(y_profile.start(), 100);
            EXPECT_EQ(y_profile.end(), 200);
            EXPECT_EQ(y_profile.mip(), 4);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 25);
            // Downsampled region is selected so that it includes the requested column
            CmpVectors<float>(y_vals,
                Segment(
                    Downsampled({reader.ReadProfileY(148), reader.ReadProfileY(149), reader.ReadProfileY(150), reader.ReadProfileY(151)}),
                    25, 50),
                1e-5);
        }
    }

    TEST_F(CursorSpatialProfileTest, Hdf5MultipleChunkFullRes) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "3000_2000_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 150, 150);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 3000);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 3000);
            CmpVectors<float>(x_vals, reader.ReadProfileX(150));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 2000);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 2000);
            CmpVectors<float>(y_vals, reader.ReadProfileY(150));
        }
    }

    TEST_F(CursorSpatialProfileTest, Hdf5MultipleChunkFullResStartEnd) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "3000_2000_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 1000, 1500), Message::SpatialConfig("y", 1000, 1500)};
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SpatialProfileData> data_vec = LoadProfiles(path_string, profiles, 1250, 1250);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 1000);
            EXPECT_EQ(x_profile.end(), 1500);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 500);
            CmpVectors<float>(x_vals, Segment(reader.ReadProfileX(1250), 1000, 1500));

            EXPECT_EQ(y_profile.start(), 1000);
            EXPECT_EQ(y_profile.end(), 1500);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 500);
            CmpVectors<float>(y_vals, Segment(reader.ReadProfileY(1250), 1000, 1500));
        }
    }

    TEST_F(CursorSpatialProfileTest, FitsChannelChange) {
        auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_3d.fits");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        FitsDataReader reader(path_string);
        
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(5, 5);
        std::string msg;
        frame->SetImageChannels(1, 0, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), 5);
            EXPECT_EQ(data.y(), 5);
            EXPECT_EQ(data.channel(), 1);
            EXPECT_EQ(data.stokes(), 0);
            CmpValues(data.value(), reader.ReadPointXY(5, 5, 1));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 10);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 10);
            CmpVectors<float>(x_vals, reader.ReadProfileX(5, 1));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 10);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 10);
            CmpVectors<float>(y_vals, reader.ReadProfileY(5, 1));
        }
    }

    TEST_F(CursorSpatialProfileTest, FitsChannelStokesChange) {
        auto path_string = (TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("Qx"), Message::SpatialConfig("Qy")};
        FitsDataReader reader(path_string);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        
        int x(5);
        int y(5);
        int channel(1);
        int stokes(0);                // set stokes channel as "I"
        int spatial_config_stokes(1); // set spatial config coordinate = {"Qx", "Qy"}

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
            EXPECT_EQ(x_profile.end(), 10);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 10);
            CmpVectors<float>(x_vals, reader.ReadProfileX(y, channel, spatial_config_stokes));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 10);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 10);
            CmpVectors<float>(y_vals, reader.ReadProfileY(x, channel, spatial_config_stokes));
        }
    }

    TEST_F(CursorSpatialProfileTest, ContiguousHDF5ChannelChange) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "10_10_2_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        Hdf5DataReader reader(path_string);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(5, 5);
        std::string msg;
        frame->SetImageChannels(1, 0, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), 5);
            EXPECT_EQ(data.y(), 5);
            EXPECT_EQ(data.channel(), 1);
            EXPECT_EQ(data.stokes(), 0);
            CmpValues(data.value(), reader.ReadPointXY(5, 5, 1));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 10);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 10);
            CmpVectors<float>(x_vals, reader.ReadProfileX(5, 1));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 10);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 10);
            CmpVectors<float>(y_vals, reader.ReadProfileY(5, 1));
        }
    }

    TEST_F(CursorSpatialProfileTest, ChunkedHDF5ChannelChange) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "1000_1000_2_row_column.hdf5");
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        Hdf5DataReader reader(path_string);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(5, 5);
        std::string msg;
        frame->SetImageChannels(1, 0, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), 5);
            EXPECT_EQ(data.y(), 5);
            EXPECT_EQ(data.channel(), 1);
            EXPECT_EQ(data.stokes(), 0);
            CmpValues(data.value(), reader.ReadPointXY(5, 5, 1));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 1000);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 1000);
            CmpVectors<float>(x_vals, reader.ReadProfileX(5, 1));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 1000);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 1000);
            CmpVectors<float>(y_vals, reader.ReadProfileY(5, 1));
        }
    }

    TEST_F(CursorSpatialProfileTest, ChunkedHDF5ChannelStokesChange) {
        auto path_string = (TestRoot() / "data" / "images" / "hdf5" / "1000_1000_2_2_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        int x(5);
        int y(5);
        int channel(1);
        int stokes(0);                // set stokes channel as "I"
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
            EXPECT_EQ(x_profile.end(), 1000);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = ProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 1000);
            CmpVectors<float>(x_vals, reader.ReadProfileX(y, channel, spatial_config_stokes));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 1000);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = ProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 1000);
            CmpVectors<float>(y_vals, reader.ReadProfileY(x, channel, spatial_config_stokes));
        }
    }
