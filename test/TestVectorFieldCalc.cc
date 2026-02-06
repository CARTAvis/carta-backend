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

std::unordered_map<CARTA::PolarizationType, float> stokes_test_values{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, 1},
    {CARTA::PolarizationType::I, 2}, {CARTA::PolarizationType::Q, 3}, {CARTA::PolarizationType::U, 4}, {CARTA::PolarizationType::V, 5}};
    
// global functions for calculating position angle (PA), PI and fPI :
// these functions are copies of functions CalcPi, CalcPa and CalcFpi in VectorField.h
bool valid(float a, float b){
   return (!std::isnan(a) && !std::isnan(b));
}   
   
float CalcPi(float q, float u, float q_error=0.00, float u_error=0.00) {
   if (valid(q,u)) {
      return ((float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - (std::pow(q_error, 2) + std::pow(u_error, 2)) / 2.0));
   }
   return FLOAT_NAN;
}

float CalcFpi(float i, float q, float u, float q_error=0.00, float u_error=0.00) {
   if (valid(q,u) && valid(i,u)) {
      float pi = CalcPi(q,u,q_error,u_error);
      return (valid(i, pi) ? (float)100.0 * (pi / i) : FLOAT_NAN);
   }
   
   return FLOAT_NAN;

}

float CalcPa(double q, double u) {   
   return (valid(q, u) ? ((float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2) : FLOAT_NAN);
}


CARTA::SetVectorOverlayParameters SourceTestMessage(
    int intensity = COMPUTED, int angle = COMPUTED, bool fractional = false, float u_error = 0, float q_error = 0, int threshold_source = NONE, float threshold = -1) {
    CARTA::SetVectorOverlayParameters message;
    message.set_stokes_intensity(intensity);
    message.set_stokes_angle(angle);
    message.set_fractional(fractional);
    message.set_debiasing(u_error && q_error);
    message.set_u_error(u_error);
    message.set_q_error(q_error);
    message.set_smoothing_factor(1); // 1 = no downsampling

   
    // TODO : when set to 1e20 or 1000  -> FillTileData fills everything with NaNs !!!??? std::nan here does not compile
    message.set_threshold_option((CARTA::PolarizationType)threshold_source);
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
    auto [test_parameters, expected_intensity, expected_angle] = GetParam();

    VectorFieldCalculator vectorfield(test_parameters);
    std::vector<CARTA::VectorOverlayTileData> messages;

    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&messages](CARTA::VectorOverlayTileData& message) {
        // copy messages :
        messages.push_back(message);
    };

    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        float value = stokes_test_values[stokes_type]; // seems that Stokes I is passed as Current
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
        // check if there is exactly 1 intensity tile:
        EXPECT_EQ(message.intensity_tiles_size(), 1);    
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

        // check if there is exactly 1 angle time:
        EXPECT_EQ(message.angle_tiles_size(), 1);
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
        TestParameters(SourceTestMessage(CURRENT, COMPUTED), 1, CalcPa(3,4)), // computed Pa
        TestParameters(SourceTestMessage(COMPUTED, CURRENT), CalcPi(3,4), 1), // computed Pi
        TestParameters(SourceTestMessage(COMPUTED, COMPUTED), CalcPi(3,4), CalcPa(3,4)), // computed Pa and Pi
        TestParameters(SourceTestMessage(COMPUTED, CURRENT, false, 0.1, 0.2), CalcPi(3, 4, 0.1, 0.2), 1), // computed Pi with errors
        TestParameters(SourceTestMessage(COMPUTED, CURRENT, true), CalcFpi(2,3,4), 1), // computed Fpi 
        TestParameters(SourceTestMessage(CURRENT, NONE, false), 1, std::numeric_limits<double>::quiet_NaN()),
        TestParameters(SourceTestMessage(NONE, CURRENT, false), std::numeric_limits<double>::quiet_NaN(), 1),
        TestParameters(
            SourceTestMessage(NONE, NONE, false), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN())));


//------------------------------------------------------ Thresholding tests below -----------------------------------------
using Pol = CARTA::PolarizationType;
const int SRC_NONE = -1, SRC_CURRENT = 0, SRC_COMPUTED = 1;
const Pol THR_CURRENT = Pol::POLARIZATION_TYPE_NONE, THR_I = Pol::I, THR_PI = Pol::Plinear, THR_FPI = Pol::PFlinear;

// perhaps make it a class with separate fields and constructor will initialise these fields based on sources of values and thresholds, and fractional ?
// using TestSpatialParameters2 = std::tuple<CARTA::SetVectorOverlayParameters, std::vector<float>, std::vector<float>, std::vector<float>, std::vector<bool> >;

CARTA::SetVectorOverlayParameters ThresholdTestMessage(
    int intensity = SRC_COMPUTED, int angle = SRC_COMPUTED, bool fractional = false, Pol threshold_source = THR_CURRENT) {
    
    float threshold = 2; // we use the same threshold for all the tests (does not have to be a parameter)
    float u_error = 0.00; // we do not have errors
    float q_error = 0.00; // we do not have errors
    
    CARTA::SetVectorOverlayParameters message;
    message.set_stokes_intensity(intensity);
    message.set_stokes_angle(angle);
    message.set_fractional(fractional);
    message.set_debiasing(u_error && q_error);
    message.set_u_error(u_error);
    message.set_q_error(q_error);
    message.set_smoothing_factor(1); // 1 = no downsampling

   
    message.set_threshold_option((CARTA::PolarizationType)threshold_source);
    message.set_threshold(threshold); 

    return message;
}

// Template test values used for data generation and as expected data in verification:
std::unordered_map<CARTA::PolarizationType, std::vector<float>> threshold_test_values{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, { 125.0, 25.0, 5.0, 1.0, 0.2}}, // reversed order of Stokes I 
    {CARTA::PolarizationType::I, { 0.2, 1.0, 5.0, 25.0, 125.0 }},                     // Stokes I intensities 
    {CARTA::PolarizationType::Q, { 1.1, 1.2, 1.3, 1.4, 1.5 }},                        // Stokes Q
    {CARTA::PolarizationType::U, { 1.1, 1.2, 1.3, 1.4, 1.5 }},                        // Stokes U
    {CARTA::PolarizationType::Plinear, { CalcPi(1.1,1.1), CalcPi(1.2,1.2), CalcPi(1.3,1.3), CalcPi(1.4,1.4), CalcPi(1.5,1.5) }}, // linear polarisation (Pi) =sqrt(Q^2+U^2)
    {CARTA::PolarizationType::PFlinear, {CalcFpi(0.2,1.1,1.1), CalcFpi(1.0,1.2,1.2), CalcFpi(5.0,1.3,1.3), CalcFpi(25.0,1.4,1.4), CalcFpi(125.0,1.5,1.5),}}, // fractional Pi = sqrt(Q^2+U2)/I * 100%
    {CARTA::PolarizationType::Pangle, { CalcPa(1.1,1.1), CalcPa(1.2,1.2), CalcPa(1.3,1.3), CalcPa(1.4,1.4), CalcPa(1.5,1.5) }} // polarisation angle (Pa) 
};

// bool list of flags showing expected positions of NaNs (true)
std::unordered_map<Pol, std::vector<bool>> nans{
   {THR_CURRENT, {false, false, false, true, true}},
   {THR_I, {true, true, false, false, false}},
   {THR_PI, {true, true, true, true, false}},
   {THR_FPI, {false, false, false, false, true}}
};

class TestSpatialParameters {
public:
    CARTA::SetVectorOverlayParameters message;
    std::vector<float> expected_intensities;
    std::vector<float> expected_intensities_current;
    std::vector<float> expected_angles;
    std::vector<bool>  expected_nans;
    
    // TestSpatialParameters(std::tuple<int, int, Pol, bool> params) { int intensity_source = std::get<0>(params);
    TestSpatialParameters( int intensity_source, int angle_source, Pol threshold_source, bool fractional ){
       message = ThresholdTestMessage(intensity_source, angle_source, fractional, threshold_source);

       if (intensity_source == SRC_CURRENT) {
          expected_intensities = threshold_test_values[CARTA::PolarizationType::POLARIZATION_TYPE_NONE]; // expected_intensities_current;
       }else{
          if (intensity_source == SRC_COMPUTED) {
             if (fractional) {
                expected_intensities = threshold_test_values[CARTA::PolarizationType::PFlinear]; // expected_fpi;
             }else{
                expected_intensities = threshold_test_values[CARTA::PolarizationType::Plinear]; // expected_computed_intensities;
             }
          }
       }

       if (angle_source == SRC_CURRENT) {
          expected_angles =  threshold_test_values[CARTA::PolarizationType::POLARIZATION_TYPE_NONE]; // was expected_intensities_current;
       }else{
          if(angle_source == SRC_COMPUTED) {
             expected_angles = threshold_test_values[CARTA::PolarizationType::Pangle];
          }
       }
       
       switch (threshold_source) {
          case THR_CURRENT:
             expected_nans = nans[THR_CURRENT];
             break;

          case THR_I:
             expected_nans = nans[THR_I];
             break;
             
          case THR_PI:
             if (fractional){
                expected_nans = nans[THR_FPI];
             }else{
                expected_nans = nans[THR_PI];
             }
             break;
             
          default:
             break;   
       }
    }    
};



// Define the parameterized test fixture
class VectorFieldThresholdingSpatialTest : public ::testing::TestWithParam<TestSpatialParameters> {
public:
    VectorFieldThresholdingSpatialTest() {}

protected:
    // You can add setup/teardown logic here if needed
};

TEST_P(VectorFieldThresholdingSpatialTest, TestThresholdingSpatial) {
//    auto [test_parameters, expected_intensities, expected_qu, expected_angles, is_nan_expected] = GetParam();
    TestSpatialParameters all_params = GetParam();    
    CARTA::SetVectorOverlayParameters& test_parameters = all_params.message;
    std::vector<float>& expected_intensities = all_params.expected_intensities;
    std::vector<float>& expected_angles = all_params.expected_angles;
    std::vector<bool>&  is_nan_expected = all_params.expected_nans;

    
    // Number of test pixels to agree in the Lambda expression below and later in initialisation of vectors actual_intensity_values and actual_angle_values
    int number_of_test_pixels = expected_intensities.size();
    
    VectorFieldCalculator vectorfield(test_parameters);
    std::vector<CARTA::VectorOverlayTileData> messages;

    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&messages](CARTA::VectorOverlayTileData& message) {
        // copy messages :
        messages.push_back(message);
    };

    
    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [&expected_intensities, &expected_angles](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        data.resize(256 * 256);
        width = 256;
        height = 256;
        
        // fill data here:
        switch( stokes_type )
        {
           case CARTA::PolarizationType::POLARIZATION_TYPE_NONE : // CURRENT :
              { 
                 std::vector<float> test_data = threshold_test_values[CARTA::PolarizationType::POLARIZATION_TYPE_NONE];
                 std::copy(test_data.begin(), test_data.begin() + 5, data.begin() );
              }
              break;
           
           case CARTA::PolarizationType::I : // Stokes I 
              {
                  std::vector<float> test_data = threshold_test_values[CARTA::PolarizationType::I];
                  std::copy(test_data.begin(), test_data.begin() + 5, data.begin() );
              }
              break;
              
           case CARTA::PolarizationType::Q :           
           case CARTA::PolarizationType::U :
              {
                  std::vector<float> test_data = threshold_test_values[stokes_type];
                  std::copy(test_data.begin(), test_data.begin() + 5, data.begin() );
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
        // check if there is exactly 1 intensity tile:
        EXPECT_EQ(message.intensity_tiles_size(), 1);
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
              EXPECT_NEAR(expected_intensities[i], actual_value, 1e-4 );
           }
        }        
        
        // check if there is exactly 1 angle tile:
        EXPECT_EQ(message.angle_tiles_size(), 1);
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
              EXPECT_NEAR(expected_angles[i], actual_value, 1e-4 );                            
           }
        }        
    }
}

/* TODO/FUTURE : 
     In the near future replace the semi-manual loop implemented in GenerateThresholdTestParameterCombinations with the structure below
     This requires newest version of gtest/gmock not just on the developers' computers but also in CARTA CI/CD environment as well - hence on hold at least until the 
     new release is finalised.

INSTANTIATE_TEST_SUITE_P(ThresholdingSpatialTests, VectorFieldThresholdingSpatialTest,
   testing::ConvertGenerator<TestSpatialParameters>( 
        testing::Combine(
           testing::ValuesIn({SRC_CURRENT, SRC_COMPUTED}),  // loop over intensity source
           testing::ValuesIn({SRC_CURRENT, SRC_COMPUTED}),  // loop over angle source
           testing::ValuesIn({THR_CURRENT, THR_I, THR_PI}), // loop over threshold source
           testing::ValuesIn({false, true})                 // loop over fractional  
        )
        [](const std::tuple<int, int, Pol, bool>& params){
           const auto [intensity_source, angle_source, threshold_source, fractional] = params;
           return {ThresholdTestMessage(SRC_CURRENT, SRC_COMPUTED, false, THR_CURRENT), ReverseParams(expected_intensities), expected_qu, expected_angles, std::vector<bool>{false, false, false, true, true}};
        }
    )
);*/

// this is the only way I could force this to compile:
std::vector<TestSpatialParameters> GenerateThresholdTestParameterCombinations() {
    std::vector<TestSpatialParameters> results;
    
    for (int intensity : {SRC_CURRENT, SRC_COMPUTED}) {
        for (int angle : {SRC_COMPUTED, SRC_CURRENT}) {  // SRC_CURRENT - this one does not work with angle not sure how it could work, actually ...
            for (Pol threshold : {THR_CURRENT, THR_I, THR_PI}) {
                for (bool fractional : {false, true}) {
                    // results.push_back(TestSpatialParameters(std::make_tuple(intensity, angle, threshold, fractional)));
                    results.push_back(TestSpatialParameters(intensity,angle,threshold,fractional));
                }
            }
        }
    }
    return results;
}

// Now instantiate using the function
INSTANTIATE_TEST_SUITE_P(
    ThresholdingSpatialTests, 
    VectorFieldThresholdingSpatialTest,
    testing::ValuesIn(GenerateThresholdTestParameterCombinations())
);    
    
    