/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "src/DataStream/VectorField.h"
#include "CommonTestUtilities.h"

using namespace carta;

class TestVectorField : public VectorField {
public:
    TestVectorField() : VectorField() {}
        
//    FRIEND_TEST(FitsImageTest, ExampleFriendTest);
     void RenewParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis){
        VectorField::RenewParameters( message, stokes_axis );
     }
};

class VectorFieldCalcTest : public ::testing::Test, public ImageGenerator {};

TEST_F(VectorFieldCalcTest, TestStokesI) {
    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters

    TestVectorField vectorfield;
    vectorfield.RenewParameters( param_message, 4 );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.angle_tiles(0); // message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = reinterpret_cast<const float*>(image_data.c_str()); // static_cast<const float*>(image_data.c_str());
          std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , 1 , 1e-8f);
          }
       };
       
       
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor, int z_index, CARTA::PolarizationType stokes_type, int& width, int& height)
    {
       data.assign(256*256,1);
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
    vectorfield.CalculateVectorField( callback, dims, z_index, getdata_callback );    
}     

TEST_F(VectorFieldCalcTest, TestStokesPa) {
    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    param_message.set_stokes_angle(1);

    TestVectorField vectorfield;
    vectorfield.RenewParameters( param_message, 4 );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;          

          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.angle_tiles(0); // message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = reinterpret_cast<const float*>(image_data.c_str()); // static_cast<const float*>(image_data.c_str());
          std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          VectorField::CalcPa calcpa;
          float expected_pa = calcpa(2,3);
          std::cout << "Expected PA = " << expected_pa << std::endl;
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , expected_pa , 1e-8f);
          }
       };
       
       
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor, int z_index, CARTA::PolarizationType stokes_type, int& width, int& height)
    {
       int value = 1;
       if (stokes_type == CARTA::PolarizationType::Q ){
          value = 2;
       }
       if (stokes_type == CARTA::PolarizationType::U ){
          value = 3;
       }
       if (stokes_type == CARTA::PolarizationType::V ){
          value = 4;
       }
    
       data.assign(256*256,value);
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
    vectorfield.CalculateVectorField( callback, dims, z_index, getdata_callback );    
}     

TEST_F(VectorFieldCalcTest, TestStokesPi) {
    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    param_message.set_stokes_intensity(1);

    TestVectorField vectorfield;
    vectorfield.RenewParameters( param_message, 4 );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;
          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = reinterpret_cast<const float*>(image_data.c_str()); // static_cast<const float*>(image_data.c_str());
          std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          VectorField::CalcPi calcpi(0.0f,0.0f);
          float expected_pi = calcpi(2,3);
          std::cout << "Expected PI = " << expected_pi << std::endl;
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , expected_pi , 1e-8f);
          }                    
       };
       
       
    auto getdata_callback = [](std::vector<float>& data, CARTA::ImageBounds& bounds, int smoothing_factor, int z_index, CARTA::PolarizationType stokes_type, int& width, int& height)
    {
       int value = 1;
       if (stokes_type == CARTA::PolarizationType::Q ){
          value = 2;
       }
       if (stokes_type == CARTA::PolarizationType::U ){
          value = 3;
       }
       if (stokes_type == CARTA::PolarizationType::V ){
          value = 4;
       }
    
       data.assign(256*256,value);
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
    vectorfield.CalculateVectorField( callback, dims, z_index, getdata_callback );    
}     

