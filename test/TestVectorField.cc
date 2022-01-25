/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "DataStream/Smoothing.h"
#include "Frame/Frame.h"
#include "ImageData/PolarizationCalculator.h"
#include "Session/Session.h"

static const std::string IMAGE_SHAPE = "11 11 25 4";
static const std::string IMAGE_OPTS = "-s 0";
static const std::string IMAGE_OPTS_NAN = "-s 0 -n row column -d 10";

class VectorFieldTest : public ::testing::Test {
public:
    class TestFrame : public Frame {
    public:
        TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
            : Frame(session_id, loader, hdu, default_z) {}

        static bool TestBlockSmooth(std::string sample_file_path, PolarizationCalculator::StokesTypes stokes_type, int mip) {
            // Open the file
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));

            PolarizationCalculator polarization_calculator(
                frame->_loader->GetImage(), AxisRange(frame->_z_index), AxisRange(ALL_X), AxisRange(ALL_Y));

            auto stokes_image = polarization_calculator.GetStokesImage(stokes_type);
            if (stokes_image == nullptr) {
                return false;
            }

            casacore::Slicer section = frame->GetImageSlicer(AxisRange(0), stokes_type);
            std::vector<float> stokes_image_data;
            if (!frame->GetSlicerData(section, stokes_image_data)) {
                return false;
            }

            // Block averaging
            const int x = 0;
            const int y = 0;
            const int req_height = frame->_width - y;
            const int req_width = frame->_height - x;
            const int num_image_columns = frame->_width;
            const int num_image_rows = frame->_height;
            size_t num_region_rows = std::ceil((float)req_height / mip);
            size_t num_region_columns = std::ceil((float)req_width / mip);

            std::vector<float> down_sampled_data(num_region_rows * num_region_columns);

            BlockSmooth(stokes_image_data.data(), down_sampled_data.data(), num_image_columns, num_image_rows, num_region_columns,
                num_region_rows, x, y, mip);

            CheckDownSampledData(
                stokes_image_data, down_sampled_data, num_image_columns, num_image_rows, num_region_columns, num_region_rows, mip);

            return true;
        }

        static bool TestCalculation(std::string sample_file_path, int mip, float q_err = 0, float u_err = 0) {
            // Open the file
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));

            // Get Stokes I, Q, and U images data
            PolarizationCalculator polarization_calculator(
                frame->_loader->GetImage(), AxisRange(frame->_z_index), AxisRange(ALL_X), AxisRange(ALL_Y));

            auto stokes_i_image = polarization_calculator.GetStokesImage(PolarizationCalculator::StokesTypes::I);
            auto stokes_q_image = polarization_calculator.GetStokesImage(PolarizationCalculator::StokesTypes::Q);
            auto stokes_u_image = polarization_calculator.GetStokesImage(PolarizationCalculator::StokesTypes::U);

            if ((stokes_i_image == nullptr) || (stokes_q_image == nullptr) || (stokes_u_image == nullptr)) {
                return false;
            }

            casacore::Slicer section_i = frame->GetImageSlicer(AxisRange(0), PolarizationCalculator::StokesTypes::I);
            casacore::Slicer section_q = frame->GetImageSlicer(AxisRange(0), PolarizationCalculator::StokesTypes::Q);
            casacore::Slicer section_u = frame->GetImageSlicer(AxisRange(0), PolarizationCalculator::StokesTypes::U);

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
            const int x = 0;
            const int y = 0;
            const int req_height = frame->_width - y;
            const int req_width = frame->_height - x;
            const int num_image_columns = frame->_width;
            const int num_image_rows = frame->_height;
            size_t num_region_rows = std::ceil((float)req_height / mip);
            size_t num_region_columns = std::ceil((float)req_width / mip);

            std::vector<float> down_sampled_i(num_region_rows * num_region_columns);
            std::vector<float> down_sampled_q(num_region_rows * num_region_columns);
            std::vector<float> down_sampled_u(num_region_rows * num_region_columns);

            BlockSmooth(stokes_i_data.data(), down_sampled_i.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);
            BlockSmooth(stokes_q_data.data(), down_sampled_q.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);
            BlockSmooth(stokes_u_data.data(), down_sampled_u.data(), num_image_columns, num_image_rows, num_region_columns, num_region_rows,
                x, y, mip);

            size_t res_size = num_region_rows * num_region_columns;
            std::vector<float> pi(res_size);
            std::vector<float> fpi(res_size);
            std::vector<float> pa(res_size);

            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pi.begin(),
                [&](float q, float u) { return sqrt(pow(q, 2) + pow(u, 2) - (pow(q_err, 2) + pow(u_err, 2)) / 2.0); });

            std::transform(
                down_sampled_i.begin(), down_sampled_i.end(), pi.begin(), fpi.begin(), [&](float i, float pi) { return (pi / i); });

            std::transform(down_sampled_q.begin(), down_sampled_q.end(), down_sampled_u.begin(), pa.begin(),
                [&](float q, float u) { return atan2(u, q) / 2; });

            // Check results
            for (int i = 0; i < res_size; ++i) {
                float expected_pi = sqrt(pow(down_sampled_q[i], 2) + pow(down_sampled_u[i], 2) - (pow(q_err, 2) + pow(u_err, 2)) / 2.0);
                float expected_fpi = expected_pi / down_sampled_i[i];
                float expected_pa = atan2(down_sampled_u[i], down_sampled_q[i]) / 2; // i.e., 0.5 * tan^-1 (U∕Q)
                EXPECT_FLOAT_EQ(pi[i], expected_pi);
                EXPECT_FLOAT_EQ(fpi[i], expected_fpi);
                EXPECT_FLOAT_EQ(pa[i], expected_pa);
            }

            return true;
        }
    };

    static void CheckDownSampledData(const std::vector<float>& src_data, const std::vector<float>& dest_data, int src_width, int src_height,
        int dest_width, int dest_height, int mip) {
        EXPECT_GE(src_data.size(), 0);
        EXPECT_GE(dest_data.size(), 0);
        if ((src_width % 2 == 0) && (src_height % 2 == 0)) {
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
                        if (!isnan(src_data[j * src_width + i])) {
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
};

TEST_F(VectorFieldTest, TestBlockSmooth) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::I, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::Q, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::U, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::V, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::I, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::Q, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::U, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::V, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::I, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::Q, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::U, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_file, PolarizationCalculator::StokesTypes::V, 8));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::I, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::Q, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::U, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::V, 2));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::I, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::Q, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::U, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::V, 4));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::I, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::Q, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::U, 8));
    EXPECT_TRUE(TestFrame::TestBlockSmooth(sample_nan_file, PolarizationCalculator::StokesTypes::V, 8));
}

TEST_F(VectorFieldTest, TestCalculation) {
    auto sample_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 2));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 4));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 8));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 2, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 4, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_file, 8, 1e-3, 1e-3));

    auto sample_nan_file = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS_NAN);
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 2));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 4));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 8));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 2, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 4, 1e-3, 1e-3));
    EXPECT_TRUE(TestFrame::TestCalculation(sample_nan_file, 8, 1e-3, 1e-3));
}
