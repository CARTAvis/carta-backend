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

    void SetUp() {
        setenv("HDF5_USE_FILE_LOCKING", "FALSE", 0);
    }
};

struct CursorSpatialProfileParams {
    std::string imageFile;
    std:vector<int> cursorDims;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles;
    std::vector<double> expectedProfile;
};

class CursorSpatialProfileTest :
    public ::testing::TestWithParam<CursorSpatialProfileParams> {};


TEST_P(CursorSpatialProfileTest, GeneratesCorrectProfile1) {
    auto params = GetParam();
    auto image = LoadImage(params.imageFile);
    auto profile = ComputeCursorSpatialProfile(image, params.smoothingMode);

    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(params.imageFile));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    FitsDataReader reader(path_string);

    frame->SetSpatialRequirements(params.profiles);
    frame->SetCursor(params.cursorDims[0], params.cursorDims[1]);

    std::vector<CARTA::SpatialProfileData> data_vec;
    frame->FillSpatialProfileData(data_vec);

    for (auto& data : data_vec) {
        EXPECT_EQ(data.file_id(), params.expectedProfile[0]);
        EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
        EXPECT_EQ(data.x(), params.expectedProfile[1]);
        EXPECT_EQ(data.y(), params.expectedProfile[2]);
        EXPECT_EQ(data.channel(), params.expectedProfile[3]);
        EXPECT_EQ(data.stokes(), params.expectedProfile[4]);
        CmpValues(data.value(), reader.ReadPointXY(params.expectedProfile[5], params.expectedProfile[6]));
        EXPECT_EQ(data.profiles_size(), params.expectedProfile[7]);

        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), params.expectedProfile[8]);
        EXPECT_EQ(x_profile.end(), params.expectedProfile[9]);
        EXPECT_EQ(x_profile.mip(), params.expectedProfile[10]);
        auto x_vals = ProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), params.expectedProfile[11]);
        CmpVectors<float>(x_vals, reader.ReadProfileX(params.expectedProfile[12]));

        EXPECT_EQ(y_profile.start(), params.expectedProfile[13]);
        EXPECT_EQ(y_profile.end(), params.expectedProfile[14]);
        EXPECT_EQ(y_profile.mip(), params.expectedProfile[15]);
        auto y_vals = ProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), params.expectedProfile[16]);
        CmpVectors<float>(y_vals, reader.ReadProfileY(params.expectedProfile[17]));
    }
}

INSTANTIATE_TEST_SUITE_P(
    VariousProfiles,
    CursorSpatialProfileTest,
    ::testing::Values(
        CursorSpatialProfileParams{ // small fits profile
            TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits",
            {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, {0, 5, 5, 0, 0, 5, 5, 2, 0, 10, 0, 10, 5, 0, 10, 0, 10, 5}}, 
        CursorSpatialProfileParams{ // small hdf5 profile
            TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "10_10_row_column.hdf5",
            {5, 5},
            {Message::SpatialConfig("x"), Message::SpatialConfig("y")}, {0, 5, 5, 0, 0, 5, 5, 2, 0, 10, 0, 10, 5, 0, 10, 0, 10, 5}}, 
        CursorSpatialProfileParams{ // low resolution fits profile
            TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits",
            {50, 50},
            {Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)}, {0, 5, 5, 0, 0, 5, 5, 2, 0, 10, 0, 10, 5, 0, 10, 0, 10, 5}} 
    )
);

    TEST_F(CursorSpatialProfileTest, LowResFitsProfile) {
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(50, 50);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "130_100_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(50, 50);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "130_100_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        // mip 4 is requested, but the file only has a dataset for mip 2
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 4), Message::SpatialConfig("y", 0, 0, 4)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(50, 50);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "120_100_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        // mip 2 is requested, but this file is too small to have mipmaps
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 0, 0, 2), Message::SpatialConfig("y", 0, 0, 2)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(50, 50);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        FitsDataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(150, 150);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "400_300_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 0), Message::SpatialConfig("y", 100, 200, 0)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(150, 150);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        FitsDataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(150, 150);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "400_300_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 100, 200, 4), Message::SpatialConfig("y", 100, 200, 4)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(150, 150);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "3000_2000_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(150, 150);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "3000_2000_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig("x", 1000, 1500), Message::SpatialConfig("y", 1000, 1500)};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(1250, 1250);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_3d.fits");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        FitsDataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "fits" / "noise_10px_10px.fits");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        FitsDataReader reader(path_string);

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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "10_10_2_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "1000_1000_2_row_column.hdf5");
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
        Hdf5DataReader reader(path_string);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
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
        auto path_string = (TestRoot() / "carta-backend-test-data" / "images" / "hdf5" / "1000_1000_2_2_row_column.hdf5");
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
