/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "src/DataStream/VectorField.h"
#include "CommonTestUtilities.h"

using namespace carta;

std::unordered_map<CARTA::PolarizationType, float> stokes_test_values_map{ {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, sqrt(2.0*2.0+3.0*3.0)},  // = sqrt(Q^2 + U^2)
                                                                             {CARTA::PolarizationType::I, sqrt(2.0*2.0+3.0*3.0)},  // = sqrt(Q^2 + U^2)
                                                                             {CARTA::PolarizationType::Q, 2}, 
                                                                             {CARTA::PolarizationType::U, 3}, 
                                                                             {CARTA::PolarizationType::V, 0}
                                                                           }; 

std::tuple<CARTA::SetVectorOverlayParameters,float,float> get_parameters(int intensity=1, int angle=1 ) {    
   CARTA::SetVectorOverlayParameters message;   
   message.set_smoothing_factor(2);       
   
   message.set_stokes_intensity(intensity);
   message.set_stokes_angle(angle);
   
   VectorFieldCalculator::CalcPa calcpa;
   float expected_intensity = stokes_test_values_map[CARTA::PolarizationType::I];
   float expected_angle     = expected_intensity;
   
   if( intensity > 0 ){   
       message.set_threshold(0);
       message.set_threshold_option(CARTA::PolarizationType::I);
       message.set_fractional(true);
   }

   if( angle > 0 ){
      expected_angle     = calcpa( stokes_test_values_map[CARTA::PolarizationType::Q] , stokes_test_values_map[CARTA::PolarizationType::U]);
   }
   
   if( intensity > 0 && angle > 0 ){
      VectorFieldCalculator::CalcPi calcpi(0,0);
      float pi = calcpi(  stokes_test_values_map[CARTA::PolarizationType::Q] , stokes_test_values_map[CARTA::PolarizationType::U] );
   
      VectorFieldCalculator::CalcFpi calcFpi;      
      expected_intensity = calcFpi( stokes_test_values_map[CARTA::PolarizationType::I] , pi );
      //printf("TEST : Expected fpi = %.4f from %.4f / %.4f * 100\n",expected_intensity,pi,stokes_test_values_map[CARTA::PolarizationType::I]);
   }
      
   return {message,expected_intensity,expected_angle};   
}


// Define the parameterized test fixture
class VectorFieldCalcParamTest : public ::testing::TestWithParam<std::tuple<CARTA::SetVectorOverlayParameters,float,float>> {
public:
    VectorFieldCalcParamTest() {}
     
protected:
    // You can add setup/teardown logic here if needed
};

// class VectorFieldCalcParamTest : public ::testing::Test, public ImageGenerator {};

class TestVectorField : public VectorFieldCalculator {
public:
   TestVectorField(const CARTA::SetVectorOverlayParameters& message, bool has_stokes_axis) : VectorFieldCalculator(message, has_stokes_axis) {}
};


TEST_P(VectorFieldCalcParamTest, TestStokes) {
    double expected_value = 1.00;
    auto [test_parameters,expected_intensity,expected_angle] = GetParam();
    
    bool has_stokes_axis = (test_parameters.stokes_angle() > 0);
    TestVectorField vectorfield( test_parameters, has_stokes_axis );
    
    // lambda expression receiving tile data (messege as sent to front-end) and checking if all values = 1 (as expected for Stokes I)
    auto callback = [&test_parameters,&expected_intensity,&expected_angle,&stokes_test_values_map](CARTA::VectorOverlayTileData& message){
          // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check intensity tiles :
          const float* float_data = reinterpret_cast<const float*>(message.intensity_tiles(0).image_data().c_str()); // static_cast<const float*>(image_data.c_str());
          int float_size = message.angle_tiles(0).image_data().size()/4;
          
          // TODO - this part still fails for some reason - I do not understand what's going on in this logic 
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , expected_intensity, 1e-8f);
          }
          
          // check angle tiles :
          if( message.angle_tiles_size() > 0 ) {
// why this is failing when comparing to expected_angle
              VectorFieldCalculator::CalcPa calcpa;
              float_data = reinterpret_cast<const float*>(message.angle_tiles(0).image_data().c_str());
              float_size = message.angle_tiles(0).image_data().size()/4;
              for(int i=0;i<float_size;i++){
                  EXPECT_NEAR( float_data[i] , expected_angle, 1e-8f);
              }
          }
       };

       
    // lambda expression producing tile data (256x256) all set to 1 for Stokes I        
    auto getdata_callback = [&stokes_test_values_map](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor, CARTA::PolarizationType stokes_type, int& width, int& height)
    {             
       float value = stokes_test_values_map[stokes_type]; // seems that Stokes I is passed as Current 
       std::cout << "DEBUG : getdata_callback stokes = " << stokes_type << " value = " << value << std::endl;
       
       data.assign(256*256,value); // generating Stokes I tile 256x256 all values = 1 
       width = 256;
       height = 256;              
       
       return true;
    };
       
    DimsInfo dims;
    dims.width = 2048;
    dims.height = 2048;
    dims.depth  = 1;
    dims.num_channels = 1;
    dims.num_stokes = 4;
    int z_index = 2;
    vectorfield.Calculate( callback, dims, getdata_callback );    
}     

// Instantiate the test suite with the desired enum values
INSTANTIATE_TEST_SUITE_P(
    StokesTests,
    VectorFieldCalcParamTest,
    ::testing::Values(get_parameters(1,0),get_parameters(0,1),get_parameters(1,1)) // TODO : last test (1,1) still fails, check if these tests make any sense at all ...
);


