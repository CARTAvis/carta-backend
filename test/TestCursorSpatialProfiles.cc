/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

/*
 * Spatial Profile Tests
 *
 * This suite of parameterized tests validates the correctness of spatial profile
 * generation in a variety of scenarios. Profiles represent intensity cuts along
 * the X and Y axes through an image at a given cursor position, and must match
 * the expected metadata, ranges, sizes, and values derived directly from the
 * underlying image data.
 *
 * Covered cases:
 *  - CorrectProfileTest: Full-resolution profiles from FITS or HDF5 images,
 *    checked against ground truth data.
 *  - LowResProfileTest: Low-resolution (decimated) profiles from HDF5 images,
 *    ensuring downsampling produces consistent results.
 *  - ChannelStokesTest: Profiles generated with different channel and Stokes
 *    selections, validated for both FITS and HDF5 images.
 *  - HDF5ChannelTest: Channel change handling in HDF5 images, including Q-profile
 *    cases, ensuring correct values and profiles.
 *  - MultiChunkTest: Profiles generated from multi-chunk HDF5 datasets, ensuring
 *    seamless handling across chunk boundaries.
 *  - LowResStartEndTest: Low-resolution profiles with specific start/end ranges
 *    and mip levels, ensuring alignment with expected spatial settings.
 *
 * Together, these tests ensure that spatial profiles remain accurate across
 * file formats, resolution levels, channel/stokes selections, and data layouts.
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

enum class ReaderType { Fits, Hdf5 };

class CursorSpatialProfileTest : public ImageGenerator {
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

    // Helper function to load spatial profiles for a given image and cursor position.
    //
    // Arguments:
    //   - path: the file path to the image.
    //   - profiles: a vector of spatial profile configuration objects.
    //   - x: the X coordinate of the cursor.
    //   - y: the Y coordinate of the cursor.
    //
    // Behavior:
    //   * Creates a FileLoader for the specified image file.
    //   * Instantiates a Frame object using the loader.
    //   * Sets the spatial profile requirements and cursor position on the frame.
    //   * Fills a vector with the resulting SpatialProfileData objects.
    //   * Returns the vector of profile data.
    //
    // This function is useful for tests that need to retrieve spatial profile data
    // for specific cursor positions and configurations, abstracting away the details
    // of frame and loader setup.
    std::vector<CARTA::SpatialProfileData> LoadProfiles(
        const std::string& path, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& profiles, int x, int y) {
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

// CorrectProfileTest:
// Verifies that spatial profiles generated from an image (FITS or HDF5) match
// expected values. It checks profile metadata (file ID, cursor position, channel,
// stokes), scalar values at the cursor, and full X/Y profile data against ground
// truth read directly from the file. Ensures the profile generation logic is
// correct and consistent with the underlying image data.

struct ExpectedProfile {
    int file_id;
    int x;
    int y;
    int channel;
    int stokes;
    int profiles_size;
    std::pair<int, int> x_range;
    int x_mip;
    size_t x_size;
    std::pair<int, int> y_range;
    int y_mip;
    size_t y_size;
};

struct CorrectProfileTestParams {
    std::string imageFile;
    std::vector<int> cursorDims;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    std::pair<int, int> cmpValues; // first: x, second: y
    ExpectedProfile expected;
    ReaderType readerType;
};

class CorrectProfileTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<CorrectProfileTestParams> {};

TEST_P(CorrectProfileTest, GeneratesCorrectProfile) {
    auto params = GetParam();
    std::vector<CARTA::SpatialProfileData> data_vec =
        LoadProfiles(params.imageFile, params.profiles, params.cursorDims[0], params.cursorDims[1]);

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
        EXPECT_EQ(y_profile.mip(), params.expected.y_mip);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), params.expected.y_size);
        CmpVectors<float>(y_vals, reader->ReadProfileY(params.cmpValues.second));
    }
}

INSTANTIATE_TEST_SUITE_P(CorrectProfiles, CorrectProfileTest,
    ::testing::Values(
        CorrectProfileTestParams{// small fits profile
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, {5, 5},
            {.file_id = 0,
                .x = 5,
                .y = 5,
                .channel = 0,
                .stokes = 0,
                .profiles_size = 2,
                .x_range = {0, 10},
                .x_mip = 0,
                .x_size = 10,
                .y_range = {0, 10},
                .y_mip = 0,
                .y_size = 10},
            ReaderType::Fits},
        CorrectProfileTestParams{// low resolution fits profile
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)}, {5, 5},
            {.file_id = 0,
                .x = 5,
                .y = 5,
                .channel = 0,
                .stokes = 0,
                .profiles_size = 2,
                .x_range = {0, 10},
                .x_mip = 0,
                .x_size = 10,
                .y_range = {0, 10},
                .y_mip = 0,
                .y_size = 10},
            ReaderType::Fits},
        CorrectProfileTestParams{// small hdf5 profile
            TestRoot() / "data" / "images" / "hdf5" / "10_10_row_column.hdf5", {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, {5, 5},
            {.file_id = 0,
                .x = 5,
                .y = 5,
                .channel = 0,
                .stokes = 0,
                .profiles_size = 2,
                .x_range = {0, 10},
                .x_mip = 0,
                .x_size = 10,
                .y_range = {0, 10},
                .y_mip = 0,
                .y_size = 10},
            ReaderType::Hdf5}));

// LowResProfileTest:
// Verifies that low-resolution (decimated) spatial profiles generated from an
// HDF5 image match expected results. It checks the number of profiles, X/Y ranges,
// mip levels, and profile sizes. Generated values are compared against decimated
// ground truth from the file, ensuring that downsampling is performed correctly.

struct ExpectedProfileLowRes {
    int profiles_size;
    std::pair<int, int> x_range;
    int x_mip;
    size_t x_size;
    std::pair<int, int> y_range;
    int y_mip;
    size_t y_size;
};

struct LowResTestParams {
    std::string imageFile;
    std::vector<int> cursorDims;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    std::pair<int, int> cmpValues; // first: x, second: y
    ExpectedProfileLowRes expected;
    ReaderType readerType;
};

class LowResProfileTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<LowResTestParams> {};

TEST_P(LowResProfileTest, GeneratesCorrectProfile) {
    auto params = GetParam();
    Hdf5DataReader reader(params.imageFile);
    std::vector<CARTA::SpatialProfileData> data_vec =
        LoadProfiles(params.imageFile, params.profiles, params.cursorDims[0], params.cursorDims[1]);

    for (auto& data : data_vec) {
        EXPECT_EQ(data.profiles_size(), params.expected.profiles_size);

        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), params.expected.x_range.first);
        EXPECT_EQ(x_profile.end(), params.expected.x_range.second);
        EXPECT_EQ(x_profile.mip(), params.expected.x_mip);
        auto x_vals = ProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), params.expected.x_size);
        CmpVectors<float>(x_vals, Decimated(reader.ReadProfileX(params.cmpValues.first, 0, 0), params.cmpValues.second));

        EXPECT_EQ(y_profile.start(), params.expected.y_range.first);
        EXPECT_EQ(y_profile.end(), params.expected.y_range.second);
        EXPECT_EQ(y_profile.mip(), params.expected.x_mip);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), params.expected.y_size);
        CmpVectors<float>(y_vals, Decimated(reader.ReadProfileX(params.cmpValues.first, 0, 0), params.cmpValues.second));
    }
}

INSTANTIATE_TEST_SUITE_P(CorrectLowResProfiles, LowResProfileTest,
    ::testing::Values(
        LowResTestParams{// low res fits profile
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)}, {50, 52},
            {.profiles_size = 2, .x_range = {0, 10}, .x_mip = 0, .x_size = 10, .y_range = {0, 10}, .y_mip = 0, .y_size = 10},
            ReaderType::Fits},
        LowResTestParams{// low res hdf5 profile, exact mip available
            TestRoot() / "data" / "images" / "hdf5" / "130_100_row_column.hdf5", {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)}, {50, 52},
            {.profiles_size = 2, .x_range = {0, 130}, .x_mip = 2, .x_size = 65, .y_range = {0, 100}, .y_mip = 2, .y_size = 50},
            ReaderType::Hdf5},
        LowResTestParams{// low res hdf5 profile, lower mip available
            TestRoot() / "data" / "images" / "hdf5" / "130_100_row_column.hdf5", {50, 50},
            {Message::SpatialConfig("x", 0, 0, 4), Message::SpatialConfig("y", 0, 0, 4)}, {50, 52},
            {.profiles_size = 2, .x_range = {0, 130}, .x_mip = 2, .x_size = 65, .y_range = {0, 100}, .y_mip = 2, .y_size = 50},
            ReaderType::Hdf5},
        LowResTestParams{// low res hdf5 profile, no mip available
            TestRoot() / "data" / "images" / "hdf5" / "120_100_row_column.hdf5", {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)}, {50, 52},
            {.profiles_size = 2, .x_range = {0, 120}, .x_mip = 0, .x_size = 120, .y_range = {0, 100}, .y_mip = 0, .y_size = 100},
            ReaderType::Hdf5}));

struct FullResStartEndParams {
    std::string imageFile;
    ReaderType readerType;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    int cursorX, cursorY;
    int start, end;
    int expectedSize;
};

class FullResStartEndTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<FullResStartEndParams> {};

TEST_P(FullResStartEndTest, GeneratesCorrectProfile) {
    auto p = GetParam();
    std::unique_ptr<DataReader> reader;
    if (p.readerType == ReaderType::Fits) {
        reader = std::make_unique<FitsDataReader>(p.imageFile);
    } else {
        reader = std::make_unique<Hdf5DataReader>(p.imageFile);
    }

    auto data_vec = LoadProfiles(p.imageFile, p.profiles, p.cursorX, p.cursorY);
    for (auto& data : data_vec) {
        auto [x_profile, y_profile] = GetProfiles(data);
        EXPECT_EQ(x_profile.start(), p.start);
        EXPECT_EQ(x_profile.end(), p.end);
        EXPECT_EQ(x_profile.mip(), 0);
        EXPECT_EQ(ProfileValues(x_profile).size(), p.expectedSize);
        EXPECT_EQ(y_profile.start(), p.start);
        EXPECT_EQ(y_profile.end(), p.end);
        EXPECT_EQ(y_profile.mip(), 0);
        EXPECT_EQ(ProfileValues(y_profile).size(), p.expectedSize);
    }
}

INSTANTIATE_TEST_SUITE_P(FullResStartEnd, FullResStartEndTest,
    ::testing::Values(
        FullResStartEndParams{// full res fits start end
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", ReaderType::Fits,
            {Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)}, 150, 150, 100, 200, 100},
        FullResStartEndParams{// full res hdf5 start end
            TestRoot() / "data" / "images" / "hdf5" / "400_300_row_column.hdf5", ReaderType::Hdf5,
            {Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)}, 150, 150, 100, 200, 100}));

// LowResStartEndTest:
// Verifies that low-resolution spatial profiles (from FITS or HDF5 images) are
// generated with the correct start/end coordinates, mip level, and profile size.
// Ensures that downsampled profiles align with the expected spatial ranges and
// resolution settings.

struct LowResStartEndParams {
    std::string imageFile;
    ReaderType readerType;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    int cursorX, cursorY;
    int start, end, mip;
    int expectedSize;
};

class LowResStartEndTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<LowResStartEndParams> {};

TEST_P(LowResStartEndTest, GeneratesCorrectProfile) {
    auto p = GetParam();
    std::unique_ptr<DataReader> reader;
    if (p.readerType == ReaderType::Fits) {
        reader = std::make_unique<FitsDataReader>(p.imageFile);
    } else {
        reader = std::make_unique<Hdf5DataReader>(p.imageFile);
    }

    auto data_vec = LoadProfiles(p.imageFile, p.profiles, p.cursorX, p.cursorY);
    for (auto& data : data_vec) {
        auto [x_profile, y_profile] = GetProfiles(data);
        EXPECT_EQ(x_profile.start(), p.start);
        EXPECT_EQ(x_profile.end(), p.end);
        EXPECT_EQ(x_profile.mip(), p.mip);
        EXPECT_EQ(ProfileValues(x_profile).size(), p.expectedSize);
        EXPECT_EQ(y_profile.start(), p.start);
        EXPECT_EQ(y_profile.end(), p.end);
        EXPECT_EQ(y_profile.mip(), p.mip);
        EXPECT_EQ(ProfileValues(y_profile).size(), p.expectedSize);
    }
}

INSTANTIATE_TEST_SUITE_P(LowResStartEnd, LowResStartEndTest,
    ::testing::Values(
        LowResStartEndParams{// low res fits start end
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", ReaderType::Fits,
            {Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)}, 150, 150, 100, 200, 4, 24},
        LowResStartEndParams{// low res hdf5 start end
            TestRoot() / "data" / "images" / "hdf5" / "400_300_row_column.hdf5", ReaderType::Hdf5,
            {Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)}, 150, 150, 100, 200, 4, 25}));

// MultiChunkTest:
// Verifies that spatial profiles are generated correctly when the underlying
// HDF5 image data is split into multiple chunks. The test checks that the X/Y
// profile ranges and sizes match expectations, ensuring that profile generation
// works seamlessly across chunk boundaries in the dataset.

struct MultiChunkParams {
    std::string imageFile;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    int cursorX, cursorY;
    int x_start, x_end, x_size;
    int y_start, y_end, y_size;
};

class MultiChunkTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<MultiChunkParams> {};

TEST_P(MultiChunkTest, GeneratesCorrectProfile) {
    auto p = GetParam();
    Hdf5DataReader reader(p.imageFile);
    auto data_vec = LoadProfiles(p.imageFile, p.profiles, p.cursorX, p.cursorY);
    for (auto& data : data_vec) {
        auto [x_profile, y_profile] = GetProfiles(data);
        EXPECT_EQ(x_profile.start(), p.x_start);
        EXPECT_EQ(x_profile.end(), p.x_end);
        EXPECT_EQ(ProfileValues(x_profile).size(), p.x_size);
        EXPECT_EQ(y_profile.start(), p.y_start);
        EXPECT_EQ(y_profile.end(), p.y_end);
        EXPECT_EQ(ProfileValues(y_profile).size(), p.y_size);
    }
}

INSTANTIATE_TEST_SUITE_P(MultiChunk, MultiChunkTest,
    ::testing::Values(
        MultiChunkParams{// hdf5 multiple chunk full res
            TestRoot() / "data" / "images" / "hdf5" / "3000_2000_row_column.hdf5",
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, 150, 150, 0, 3000, 3000, 0, 2000, 2000},
        MultiChunkParams{// hdf5 multiple chunk full res start end
            TestRoot() / "data" / "images" / "hdf5" / "3000_2000_row_column.hdf5",
            {Message::SpatialConfig("x", 1000, 1500), Message::SpatialConfig("y", 1000, 1500)}, 1250, 1250, 1000, 1500, 500, 1000, 1500,
            500}));

// ChannelStokesTest:
// Verifies that spatial profiles are generated correctly when different image
// channels and Stokes parameters are requested. Works with both FITS and HDF5
// readers, applying channel/stokes selection via a Frame before generating
// profiles. Checks that reported channel/stokes match expectations and that the
// X/Y profile ranges and sizes align with the expected values.

struct ChannelStokesParams {
    std::string imageFile;
    ReaderType readerType;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    int cursorX, cursorY;
    int channel, stokes, expected_stokes;
    int x_start, x_end, x_size;
    int y_start, y_end, y_size;
};

class ChannelStokesTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<ChannelStokesParams> {};

TEST_P(ChannelStokesTest, GeneratesCorrectProfile) {
    auto p = GetParam();
    std::unique_ptr<DataReader> reader;
    if (p.readerType == ReaderType::Fits) {
        reader = std::make_unique<FitsDataReader>(p.imageFile);
    } else {
        reader = std::make_unique<Hdf5DataReader>(p.imageFile);
    }

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(p.imageFile));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    frame->SetSpatialRequirements(p.profiles);
    frame->SetCursor(p.cursorX, p.cursorY);
    std::string msg;
    frame->SetImageChannels(p.channel, p.stokes, msg);

    std::vector<CARTA::SpatialProfileData> data_vec;
    frame->FillSpatialProfileData(data_vec);

    for (auto& data : data_vec) {
        EXPECT_EQ(data.channel(), p.channel);
        EXPECT_EQ(data.stokes(), p.expected_stokes);
        auto [x_profile, y_profile] = GetProfiles(data);
        EXPECT_EQ(x_profile.start(), p.x_start);
        EXPECT_EQ(x_profile.end(), p.x_end);
        EXPECT_EQ(ProfileValues(x_profile).size(), p.x_size);
        EXPECT_EQ(y_profile.start(), p.y_start);
        EXPECT_EQ(y_profile.end(), p.y_end);
        EXPECT_EQ(ProfileValues(y_profile).size(), p.y_size);
    }
}

INSTANTIATE_TEST_SUITE_P(ChannelStokes, ChannelStokesTest,
    ::testing::Values(
        ChannelStokesParams{// fits channel change
            TestRoot() / "data" / "images" / "fits" / "noise_3d.fits", ReaderType::Fits,
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, 5, 5, 1, 0, 0, 0, 10, 10, 0, 10, 10},
        ChannelStokesParams{// fits channel and stokes change
            TestRoot() / "data" / "images" / "fits" / "noise_10px_10px.fits", ReaderType::Fits,
            {Message::SpatialConfig("Qx"), Message::SpatialConfig("Qy")}, 5, 5, 1, 0, 1, 0, 10, 10, 0, 10, 10}));

// HDF5ChannelTest:
// Verifies that spatial profiles are generated correctly when changing channels
// in an HDF5 image. A Frame is configured with spatial requirements, a fixed
// cursor, and channel/stokes selection. The test checks metadata (file ID, region,
// cursor, channel, stokes) and compares scalar values and X/Y profiles against
// ground truth read directly from the file. It also handles the case where
// Q-profiles are used, ensuring that both normal and Q-profile channel selections
// yield consistent results.

struct HDF5ChannelParams {
    std::filesystem::path path;
    int expected_size;
    int channel;
    int stokes;
    int expected_stokes;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    bool use_q_profiles; // whether to use Qx/Qy profile readers
};

class HDF5ChannelTest : public CursorSpatialProfileTest, public ::testing::TestWithParam<HDF5ChannelParams> {};

TEST_P(HDF5ChannelTest, HDF5ChannelChange) {
    auto params = GetParam();

    std::unique_ptr<DataReader> reader = std::make_unique<Hdf5DataReader>(params.path);
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.path));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    frame->SetSpatialRequirements(params.profiles);
    frame->SetCursor(5, 5);
    std::string msg;
    frame->SetImageChannels(params.channel, params.stokes, msg);

    std::vector<CARTA::SpatialProfileData> data_vec;
    frame->FillSpatialProfileData(data_vec);

    for (auto& data : data_vec) {
        EXPECT_EQ(data.file_id(), 0);
        EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
        EXPECT_EQ(data.x(), 5);
        EXPECT_EQ(data.y(), 5);
        EXPECT_EQ(data.channel(), params.channel);
        EXPECT_EQ(data.stokes(), params.expected_stokes);

        if (params.use_q_profiles) {
            CmpValues(data.value(), reader->ReadPointXY(5, 5, params.channel, params.expected_stokes));
        } else {
            CmpValues(data.value(), reader->ReadPointXY(5, 5, params.channel, params.stokes));
        }

        EXPECT_EQ(data.profiles_size(), 2);
        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), 0);
        EXPECT_EQ(x_profile.end(), params.expected_size);
        EXPECT_EQ(x_profile.mip(), 0);
        auto x_vals = ProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), params.expected_size);
        if (params.use_q_profiles) {
            CmpVectors<float>(x_vals, reader->ReadProfileX(5, params.channel, params.expected_stokes));
        } else {
            CmpVectors<float>(x_vals, reader->ReadProfileX(5, params.channel, params.stokes));
        }

        EXPECT_EQ(y_profile.start(), 0);
        EXPECT_EQ(y_profile.end(), params.expected_size);
        EXPECT_EQ(y_profile.mip(), 0);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), params.expected_size);
        if (params.use_q_profiles) {
            CmpVectors<float>(y_vals, reader->ReadProfileY(5, params.channel, params.expected_stokes));
        } else {
            CmpVectors<float>(y_vals, reader->ReadProfileY(5, params.channel, params.stokes));
        }
    }
}

INSTANTIATE_TEST_SUITE_P(HDF5Channel, HDF5ChannelTest,
    ::testing::Values(
        HDF5ChannelParams{// Contiguous HDF5 Channel Change
            TestRoot() / "data/images/hdf5/10_10_2_row_column.hdf5", 10, 1, 0, 0,
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, false},
        HDF5ChannelParams{// Chunked HDF5 Channel Change
            TestRoot() / "data/images/hdf5/1000_1000_2_row_column.hdf5", 1000, 1, 0, 0,
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, false},
        HDF5ChannelParams{// Chunked HDF5 Channel Stokes Change
            TestRoot() / "data/images/hdf5/1000_1000_2_2_row_column.hdf5", 1000, 1, 0, 1,
            {Message::SpatialConfig("Qx"), Message::SpatialConfig("Qy")}, true}));
