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

using ::testing::Pointwise;
using ::testing::FloatNear;
using ::testing::FloatEq;
using ::testing::Each;

std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, 1 }, // sqrt(3*3+4*4) 
    {CARTA::PolarizationType::I, 2 },
    {CARTA::PolarizationType::Q, 3},
    {CARTA::PolarizationType::U, 4},
    {CARTA::PolarizationType::V, 5}
};

CARTA::SetVectorOverlayParameters SourceTestMessage(int intensity = 1, int angle = 1, bool fractional = false, int u_error = 0, int q_error = 0) {
    CARTA::SetVectorOverlayParameters message;
    message.set_stokes_intensity(intensity);
    message.set_stokes_angle(angle);    
    message.set_fractional(fractional);
    message.set_debiasing(u_error && q_error);
    message.set_u_error(u_error);
    message.set_q_error(q_error);
    message.set_smoothing_factor(1); // 1 = no downsampling
// when set to 1e20 or 1000  -> FillTileData fills everything with NaNs !!!??? std::nan here does not compile    
    message.set_threshold(1); // disable threshold , std::nan does not compile !!!

    return message;
}



// Define the parameterized test fixture
class VectorFieldCalcParamTest : public ::testing::TestWithParam<std::tuple<CARTA::SetVectorOverlayParameters, float, float>> {
// class VectorFieldCalcParamTest : public ::testing::TestWithParam<CARTA::SetVectorOverlayParameters> {
public:
    VectorFieldCalcParamTest() {}

protected:
    // You can add setup/teardown logic here if needed
};

/*MATCHER_P(AllElementsEqualTo, value,
          std::string(negation ? "not all elements are equal to " : "all elements are equal to ") +
              testing::PrintToString(value)) {
    return std::all_of(arg.begin(), arg.end(), [&](const auto& elem) {
        return elem == value;
    });
*/

TEST_P(VectorFieldCalcParamTest, TestStokes) {
    // the reason to have local variable is to not capture global variables in lambda expressions (only local variables)
    // as this does not compile on MacOS (fails CI/CD on github)
    std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map_local = stokes_test_values_map;
    auto [test_parameters, expected_intensity, expected_angle] = GetParam();

    // TODO : to be added as one more parameter:
    bool has_stokes_axis = true; // (test_parameters.stokes_angle() > 0);
    VectorFieldCalculator vectorfield(test_parameters, has_stokes_axis);
    
    std::vector<float> actual_intensity, actual_angle;

    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&actual_intensity, &actual_angle](CARTA::VectorOverlayTileData& message) {
        // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " <<
        // message.angle_tiles_size() << std::endl;
        EXPECT_EQ(message.intensity_tiles_size(), 1);
        EXPECT_EQ(message.angle_tiles_size(), 1);

        // check intensity tiles :
        const float* float_data = reinterpret_cast<const float*>( message.intensity_tiles(0).image_data().c_str()); // static_cast<const float*>(image_data.c_str());
        int float_size = message.intensity_tiles(0).image_data().size() / 4;
        for (int i = 0; i < float_size; i++) {
            actual_intensity.push_back(float_data[i]);
        }
        
         if (message.angle_tiles_size() > 0) {
             float_data = reinterpret_cast<const float*>(message.angle_tiles(0).image_data().c_str());
             float_size = message.angle_tiles(0).image_data().size() / 4;
             for (int i = 0; i < float_size; i++) {
                 actual_angle.push_back(float_data[i]);
             }
         }
            
    };

    // lambda expression producing tile data (256x256) all set to 1 for Stokes I
    auto getdata_callback = [&stokes_test_values_map_local](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor,
                                CARTA::PolarizationType stokes_type, int& width, int& height) {
        float value = stokes_test_values_map_local[stokes_type]; // seems that Stokes I is passed as Current
        std::cout << "DEBUG : getdata_callback stokes = " << stokes_type << " value = " << value << std::endl;

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
    
    // TODO - use some other matcher (Each or something) see : https://google.github.io/googletest/reference/matchers.html
    EXPECT_THAT( actual_intensity, Each(expected_intensity) );
    EXPECT_THAT( actual_angle, Each(expected_angle) ); // ??? WARNING/QUESTION : does each use FloatNear or similar Float-like comparison ?
    
    
    // TODO : I do not trust the above yet : 
/*    for (int i = 0; i < actual_intensity.size(); i++) {
       EXPECT_NEAR(actual_intensity[i], expected_intensity, 1e-8f);
    }
    for (int i = 0; i < actual_angle.size(); i++) {
       EXPECT_NEAR(actual_angle[i], expected_angle, 1e-8f);
    }*/
}

// Instantiate the test suite with the desired enum values
INSTANTIATE_TEST_SUITE_P(StokesTests, VectorFieldCalcParamTest,
    //  ((float)(180.0 / M_PI) * std::atan2(3, 4) / 2)
    ::testing::Values( std::tuple<CARTA::SetVectorOverlayParameters, float, float>(SourceTestMessage(VectorFieldCalculator::CURRENT, VectorFieldCalculator::CURRENT), 1, 1),
                       std::tuple<CARTA::SetVectorOverlayParameters, float, float>(SourceTestMessage(VectorFieldCalculator::CURRENT, VectorFieldCalculator::COMPUTED), 1, ((float)(180.0 / M_PI) * std::atan2(4, 3) / 2))
    )
);
