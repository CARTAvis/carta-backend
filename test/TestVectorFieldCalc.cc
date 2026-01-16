/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
// #include <gmock/gmock.h> // For custom matchers

#include "CommonTestUtilities.h"
#include "src/DataStream/VectorField.h"

using namespace carta;

using ::testing::Each;
using ::testing::FloatEq;
using ::testing::FloatNear;
// using ::testing::IsNaN;
using ::testing::NanSensitiveDoubleNear;

enum SOURCE { NONE = -1, CURRENT = 0, COMPUTED = 1, PI = 14 }; // OTHER is for other threshold value for threshold to use COMPUTED PI ( as PolarizationType::Plinear = 14 )

std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, 1}, // sqrt(3*3+4*4)
    {CARTA::PolarizationType::I, 2}, {CARTA::PolarizationType::Q, 3}, {CARTA::PolarizationType::U, 4}, {CARTA::PolarizationType::V, 5}};

CARTA::SetVectorOverlayParameters SourceTestMessage(
    int intensity = COMPUTED, int angle = COMPUTED, bool fractional = false, float u_error = 0, float q_error = 0, int treshold_source = NONE, float threshold = -1) {
    CARTA::SetVectorOverlayParameters message;
    message.set_stokes_intensity(intensity);
    message.set_stokes_angle(angle);
    message.set_fractional(fractional);
    message.set_debiasing(u_error && q_error);
    message.set_u_error(u_error);
    message.set_q_error(q_error);
    message.set_smoothing_factor(1); // 1 = no downsampling

   
    // TODO : when set to 1e20 or 1000  -> FillTileData fills everything with NaNs !!!??? std::nan here does not compile
    message.set_threshold_option((CARTA::PolarizationType)treshold_source);
    if (threshold > 0 ) {
       message.set_threshold(threshold); 
    } else {
       message.set_threshold(std::numeric_limits<double>::quiet_NaN()); // disable threshold , std::nan does not compile !!!
    }

    return message;
}

using TestParameters = std::tuple<CARTA::SetVectorOverlayParameters, float, float>;

// Define the parameterized test fixture
class VectorFieldCalcParamTest : public ::testing::TestWithParam<TestParameters> {
public:
    VectorFieldCalcParamTest() {}

protected:
    // You can add setup/teardown logic here if needed
};

TEST_P(VectorFieldCalcParamTest, TestStokes) {
    std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map_local = stokes_test_values_map;
    auto [test_parameters, expected_intensity, expected_angle] = GetParam();

    VectorFieldCalculator vectorfield(test_parameters);
    std::vector<CARTA::VectorOverlayTileData> messages;

    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&messages](CARTA::VectorOverlayTileData& message) {
        // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " <<
        // message.angle_tiles_size() << std::endl;
        EXPECT_EQ(message.intensity_tiles_size(), 1);
        EXPECT_EQ(message.angle_tiles_size(), 1);

        // copy messages :
        messages.push_back(message);
    };

    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        float value = stokes_test_values_map[stokes_type]; // seems that Stokes I is passed as Current
        // std::cout << "DEBUG : getdata_callback stokes = " << stokes_type << " value = " << value << std::endl;

        data.assign(256 * 256, value); // generating Stokes I tile 256x256 all values = 1
        width = 256;
        height = 256;

        return true;
    };

    DimsInfo dims;
    dims.width = 2048;
    dims.height = 2048;
    dims.depth = 1;
    dims.num_channels = 1;
    dims.num_stokes = 4;
    int z_index = 2;
    vectorfield.Calculate(callback, dims, getdata_callback);

    // check if intensities are as expected:
    for (auto message : messages) {
        auto data = message.intensity_tiles(0).image_data();
        auto float_data = reinterpret_cast<const float*>(data.data());
        int float_size = data.size() / sizeof(float);

        if (std::isnan(expected_intensity)) {
            EXPECT_EQ(float_size, 0);
        } else {
            std::vector<float> actual_intensity_values(float_data, float_data + float_size);
            EXPECT_THAT(actual_intensity_values, Each(FloatNear(expected_intensity, 1e-5)));
        }
        // std::cout << "TEST intesities : " << actual_intensity_values[0] << " float_size = " << float_size << std::endl;

        data = message.angle_tiles(0).image_data();
        float_data = reinterpret_cast<const float*>(data.data());
        float_size = data.size() / sizeof(float);

        if (std::isnan(expected_angle)) {
            EXPECT_EQ(float_size, 0);
        } else {
            std::vector<float> actual_angle_values(float_data, float_data + float_size);
            EXPECT_THAT(actual_angle_values, Each(FloatNear(expected_angle, 1e-5)));
        }
        // std::cout << "TEST angles : " << actual_angle_values[0] << " float_size = " << float_size << std::endl;
    }
}

// Instantiate the test suite with the desired enum values
INSTANTIATE_TEST_SUITE_P(StokesTests, VectorFieldCalcParamTest,
    ::testing::Values(TestParameters(SourceTestMessage(CURRENT, CURRENT), 1, 1),
        TestParameters(SourceTestMessage(CURRENT, COMPUTED), 1, ((float)(180.0 / M_PI) * std::atan2(4, 3) / 2)), // computed PA
        TestParameters(SourceTestMessage(COMPUTED, CURRENT), sqrt(3 * 3 + 4 * 4),
            1), // Q=3 and U=4 -> Computed I=Q^2 + U^2
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED), sqrt(3 * 3 + 4 * 4),
            ((float)(180.0 / M_PI) * std::atan2(4, 3) / 2)), // computed PA and Stokes I (as above)
        TestParameters(SourceTestMessage(COMPUTED, CURRENT, false, 0.1, 0.2),
            ((float)std::sqrt(std::pow(3, 2) + std::pow(4, 2) - (std::pow(0.1, 2) + std::pow(0.2, 2)) / 2.0)), 1),
        // Another way TBC : CalcPi(0.1,0.2)(3, 4), 1), // de-biasing with errors in Q and U
        TestParameters(SourceTestMessage(COMPUTED, CURRENT, true), (sqrt(3 * 3 + 4 * 4) / 2) * 100.00,
            1), // fractional=true : COMPUTED_STOKES/TEST_STOKES_I*100% = 5/2*100
        TestParameters(SourceTestMessage(CURRENT, NONE, false), 1, std::numeric_limits<double>::quiet_NaN()),
        TestParameters(SourceTestMessage(NONE, CURRENT, false), std::numeric_limits<double>::quiet_NaN(), 1),
        TestParameters(
            SourceTestMessage(NONE, NONE, false), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN())));


//------------------------------------------------------ Thresholding tests below -----------------------------------------
std::unordered_map<CARTA::PolarizationType, float> threshold_test_values_map{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, 4}, // NONE
    {CARTA::PolarizationType::I, 2},                      // CURRENT
    {CARTA::PolarizationType::Q, 1},                     
    {CARTA::PolarizationType::U, 1}, 
    {CARTA::PolarizationType::V, 0}
};


// Define the parameterized test fixture
class VectorFieldThresholdingTest : public ::testing::TestWithParam<TestParameters> {
public:
    VectorFieldThresholdingTest() {}

protected:
    // You can add setup/teardown logic here if needed
};


TEST_P(VectorFieldThresholdingTest, TestThresholding) {
    std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map_local = stokes_test_values_map;
    auto [test_parameters, expected_intensity, expected_angle] = GetParam();

    VectorFieldCalculator vectorfield(test_parameters);
    std::vector<CARTA::VectorOverlayTileData> messages;

    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&messages](CARTA::VectorOverlayTileData& message) {
        // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " <<
        // message.angle_tiles_size() << std::endl;
        EXPECT_EQ(message.intensity_tiles_size(), 1);
        EXPECT_EQ(message.angle_tiles_size(), 1);

        // copy messages :
        messages.push_back(message);
    };

    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        float value = threshold_test_values_map[stokes_type]; // seems that Stokes I is passed as Current
        // std::cout << "DEBUG : getdata_callback stokes = " << stokes_type << " value = " << value << std::endl;

        data.assign(256 * 256, value); // generating Stokes I tile 256x256 all values = 1
        width = 256;
        height = 256;

        return true;
    };

    DimsInfo dims;
    dims.width = 2048;
    dims.height = 2048;
    dims.depth = 1;
    dims.num_channels = 1;
    dims.num_stokes = 4;
    int z_index = 2;
    vectorfield.Calculate(callback, dims, getdata_callback);
    
//    auto IsNanMy = [](float value){ return std::isnan(value); };

    // check if intensities are as expected:
    for (auto message : messages) {
        auto data = message.intensity_tiles(0).image_data();
        auto float_data = reinterpret_cast<const float*>(data.data());
        int float_size = data.size() / sizeof(float);

        std::vector<float> actual_intensity_values(float_data, float_data + float_size);
        if (std::isnan(expected_intensity)) {
///            EXPECT_THAT(actual_intensity_values, Each(NanSensitiveDoubleNear(expected_intensity, 1e-5))); 
            // There seem to be no single-line way to check for NaNs without creating own "matcher" - leaving for later
            for (float val : actual_intensity_values) {
               EXPECT_TRUE(std::isnan(val)) << "Expected NaN, but got " << val; // Explicit check
            } 
        } else {
            EXPECT_THAT(actual_intensity_values, Each(FloatNear(expected_intensity, 1e-5)));
        }
        // std::cout << "TEST intesities : " << actual_intensity_values[0] << " float_size = " << float_size << std::endl;

        data = message.angle_tiles(0).image_data();
        float_data = reinterpret_cast<const float*>(data.data());
        float_size = data.size() / sizeof(float);

        std::vector<float> actual_angle_values(float_data, float_data + float_size);
        if (std::isnan(expected_angle)) {
//            EXPECT_THAT(actual_angle_values, Each(NanSensitiveDoubleNear(expected_angle, 1e-5))); 
           // There seem to be no single-line way to check for NaNs without creating own "matcher" - leaving for later
           for (float val : actual_angle_values) {
               EXPECT_TRUE(std::isnan(val)) << "Expected NaN, but got " << val; // Explicit check
            } 
        } else {
            EXPECT_THAT(actual_angle_values, Each(FloatNear(expected_angle, 1e-5))); // or just use NanSensitiveDoubleNear(expected_intensity, 1e-5))
        }
        // std::cout << "TEST angles : " << actual_angle_values[0] << " float_size = " << float_size << std::endl;
    }
}

// I believe these 8 tests, check all the threshold options, but possible not all combination of threshold and intensity/angle source option.
// I think this ok, but can expend further
INSTANTIATE_TEST_SUITE_P(ThresholdingTests, VectorFieldThresholdingTest,
    ::testing::Values( 
        // Testing threshold with Current Stokes I, fractional = false, threshold on current Stokes = 4 (see threshold_test_values_map)
        TestParameters(SourceTestMessage(CURRENT, COMPUTED, false, 0, 0, CURRENT, 3.9), 4, ((float)(180.0 / casacore::C::pi) * std::atan2(1, 1) / 2) ),                       // Current Stokes above threshold (4>3.9), threshold on CURRENT
        TestParameters(SourceTestMessage(CURRENT, COMPUTED, false, 0, 0, CURRENT, 4.1), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() ), // Current Stokes below threshold (4<4.1), threshold on CURRENT -> expected value is NaN for both intensity and angle
        
        // Intensity source is computed Stokes I = sqrt(2)
        // fractional = false, threshold on actual Stokes I (=2 see threshold_test_values_map) without fractional 
        // see initialisation of _threshold_source _threshold_option == 1 ? Source::I in VectorFieldCalculator constructor
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, false, 0, 0, COMPUTED, 1.99), sqrt(2), ((float)(180.0 / casacore::C::pi) * std::atan2(1, 1) / 2) ), // Stokes I computed and threshold source is Stokes I = 2 (not fractional) -> above threshold 2 > 1.99
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, false, 0, 0, COMPUTED, 2.01), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() ), //  Stokes I coputed and threshold source is Stokes I = 2 (not fractional) -> below threshold 2 < 2.01
        
        // Intensity source is computed Stokes I = sqrt(2)
        // Testing threshold with PI = sqrt(2), fractional = false, treshold source = PI = 14 -> check threshold against polarised intensity = sqrt(2)
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, false, 0, 0, PI, 1.40), sqrt(2), ((float)(180.0 / casacore::C::pi) * std::atan2(1, 1) / 2) ),                 // sqrt(2) > 1.4 -> ok values expected
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, false, 0, 0, PI, 1.42), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() ), // sqrt(2) < 1.42 -> NaNs expected
        
        // Intensity source is computed fractional polarised intensity = sqrt(2)/2 * 100% ~= 70.71%
        // Testing threshold with fPI = sqrt(2)/2 * 100% ~= 70.71%, fractional = true, treshold source = PI = 14 -> check threshold against fractional polarised intensity ~= 70.71%
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, true, 0, 0, PI, 69.00), (sqrt(2)/2.00)*100.00, ((float)(180.0 / casacore::C::pi) * std::atan2(1, 1) / 2) ),  // fPI = 70.71% > 69% -> ok values expected
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED, true, 0, 0, PI, 72.00), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() ) // fPI = 70.71% < 72% -> NaNs expected
      )       
    ); 
    
    