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
using TestSpatialParameters = std::tuple<CARTA::SetVectorOverlayParameters, std::vector<float>, std::vector<float>, std::vector<bool> >;

// Define the parameterized test fixture
class VectorFieldThresholdingSpatialTest : public ::testing::TestWithParam<TestSpatialParameters> {
public:
    VectorFieldThresholdingSpatialTest() {}

protected:
    // You can add setup/teardown logic here if needed
};


TEST_P(VectorFieldThresholdingSpatialTest, TestThresholdingSpatial) {
    std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map_local = stokes_test_values_map;
    auto [test_parameters, expected_intensities, expected_angles, is_nan_expected] = GetParam();

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
    
    // Number of test pixels to agree in the Lambda expression below and later in initialisation of vectors actual_intensity_values and actual_angle_values
    int number_of_test_pixels = 5;

    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        data.resize(256 * 256);
        width = 256;
        height = 256;
        
        // fill data here:
        switch( stokes_type )
        {
           case CARTA::PolarizationType::POLARIZATION_TYPE_NONE :
              {
                  data[0] = 0.2;
                  data[1] = 1.0;
                  data[2] = 5.0;
                  data[3] = 25.0;
                  data[4] = 125.0;
              }
              break;

           case CARTA::PolarizationType::I :           
              {
                  data[0] = 0.2;
                  data[1] = 1.0;
                  data[2] = 5.0;
                  data[3] = 25.0;
                  data[4] = 125.0;
              }
              break;
              
           case CARTA::PolarizationType::Q :           
           case CARTA::PolarizationType::U :
              {
                  data[0] = 1.1; // I = 1.55
                  data[1] = 1.2; // I = 1.70
                  data[2] = 1.3; // I = 1.84                
                  data[3] = 1.4; // I = 1.98
                  data[4] = 1.5; // I = 2.12 
              }
              break;
 
           default : 
              break;
        }

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

        std::vector<float> actual_intensity_values(float_data, float_data + number_of_test_pixels); // was + float_size

        // "manual comparison" excluding NaNs :
        for(int i=0;i<actual_intensity_values.size();i++){
           float actual_value = actual_intensity_values[i];
//           printf("ACTUAL VALUE at %d = %.8f\n",i,actual_value);
           if( is_nan_expected[i] ){
              EXPECT_TRUE(std::isnan(actual_value)) << "Intensity at pixel " << i << " expected to be NaN, but got " << actual_value;
           }else{
              // at index i a non-NaN value is expected check:
              EXPECT_NEAR(expected_intensities[i], actual_value, 1e-5 );                            
           }
        }        
        
        data = message.angle_tiles(0).image_data();
        float_data = reinterpret_cast<const float*>(data.data());
        float_size = data.size() / sizeof(float);

        std::vector<float> actual_angle_values(float_data, float_data + number_of_test_pixels); // was + float_size
        for(int i=0;i<actual_angle_values.size();i++){
           float actual_value = actual_angle_values[i];
           if( is_nan_expected[i] ){
              EXPECT_TRUE(std::isnan(actual_value)) << "Angle at pixel " << i << " expected to be NaN, but got " << actual_value;              
           }else{
              // at index i a non-NaN value is expected check:
              EXPECT_NEAR(expected_angles[i], actual_value, 1e-5 );                            
           }
        }        
        
    }
    
}

// expected intensity and angle values for 5 test pixels - based on the values "generated" in the lambda above
std::vector<float> expected_intensities = { 0.2, 1.0, 5.0, 25.0, 125.0 };
std::vector<float> expected_angles      = { 22.5, 22.5, 22.5, 22.5, 22.5 }; // Q=U -> angle = (float)(180.0 / casacore::C::pi) * std::atan2(U, Q) / 2) = 45deg/2 = 22.5 deg
    
INSTANTIATE_TEST_SUITE_P(ThresholdingSpatialTests, VectorFieldThresholdingSpatialTest,
    ::testing::Values( 
        // fractional = false, threshold on current Stokes = 2 vs. 5 pixels : 0.2, 1.0, 5.0, 25.0, 125.0 (values from CARTA::PolarizationType::POLARIZATION_TYPE_NONE in Lambda-switch above)
        TestSpatialParameters(SourceTestMessage(CURRENT, COMPUTED, false, 0, 0, CURRENT,  2.0), expected_intensities, expected_angles, std::vector<bool>{true, true, false, false, false} ), 

        // fractional = false, threshold on computed stokes = 2 vs. Stokes I computed same as CURRENT (values from CARTA::PolarizationType::I in Lambda-switch above)
        TestSpatialParameters(SourceTestMessage(CURRENT, COMPUTED, false, 0, 0, COMPUTED, 2.0), expected_intensities, expected_angles, std::vector<bool>{true, true, false, false, false} ), 
        
        // fractional = false, threshold on PI = sqrt(Q^2+U^2) = 1.55, 1.70, 1.84, 1.90, 2.12 for Q,U values from case CARTA::PolarizationType::Q/U in Lambda-switch above
        TestSpatialParameters(SourceTestMessage(CURRENT, COMPUTED, false, 0, 0, PI, 2.0), expected_intensities, expected_angles, std::vector<bool>{true, true, true, true, false} ),
        
        // fractional = true, threshold applied to computed fractional polarised intensity = sqrt(Q^2+U^2)/I * 100% = 775, 170, 36.8, 7.6, 1.696 (WARNING : unphysical but fine for testing)
        TestSpatialParameters(SourceTestMessage(CURRENT, COMPUTED, true, 0, 0, PI, 2.0), expected_intensities, expected_angles, std::vector<bool>{false, false, false, false, true} )        
      )       
    ); 
    
    