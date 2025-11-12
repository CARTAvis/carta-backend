/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

// Allows testing of protected methods in Frame without polluting the original class
// was public ::testing::Test
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
        
//    void SetUp() override {
//       std::cout << "DEBUG : TestFrame::SetUp" << std::endl;
//    }

    void callback(CARTA::VectorOverlayTileData& message)
    {
       std::cout << "DEBUG : received a message" << std::endl;
    }
        
    FRIEND_TEST(FitsImageTest, ExampleFriendTest);
};

class VectorFieldCalcTest : public ::testing::Test, public ImageGenerator {};

TEST_F(VectorFieldCalcTest, TestStokesI) {
// TODO : how to generate images with 4 different values for IQUV - add option to make_image.py or create image myself ?
//    auto path_string = GeneratedFitsImagePath("2048 2048 1 4"); // 4 Stokes 
    std::string path_string = "/home/msok/github/CARTA/code/branches/issue1526/carta-backend/test/four_stokes.fits";
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);    
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
    
//    char szCmd[1024];
//    sprintf(szCmd,"cp %s /tmp/fits/",path_string.c_str());
//    system(szCmd);

    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    // TODO : add more set_ calls ???
    
    frame->SetVectorOverlayParameters( param_message );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
//          std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.angle_tiles(0); // message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = (const float*)image_data.c_str(); // static_cast<const float*>(image_data.c_str());
//          std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , 1 , 1e-8f);
          }
       };
       
    frame->CalculateVectorField( callback );   
}

TEST_F(VectorFieldCalcTest, TestStokesPa) {
// TODO : how to generate images with 4 different values for IQUV - add option to make_image.py or create image myself ?
//    auto path_string = GeneratedFitsImagePath("2048 2048 1 4"); // 4 Stokes 
    std::string path_string = "/home/msok/github/CARTA/code/branches/issue1526/carta-backend/test/four_stokes.fits";
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);    
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
    
//    char szCmd[1024];
//    sprintf(szCmd,"cp %s /tmp/fits/",path_string.c_str());
//    system(szCmd);

    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    param_message.set_stokes_angle(1);
    
    frame->SetVectorOverlayParameters( param_message );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;
          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.angle_tiles(0); // message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = (const float*)image_data.c_str(); // static_cast<const float*>(image_data.c_str());
          // std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          VectorField::CalcPa calcpa;
          float expected_pa = calcpa(2,3);
          // std::cout << "Expected PA = " << expected_pa << std::endl;
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , expected_pa , 1e-8f);
          }
       };
       
    frame->CalculateVectorField( callback );   
}

TEST_F(VectorFieldCalcTest, TestStokesPi) {
// TODO : how to generate images with 4 different values for IQUV - add option to make_image.py or create image myself ?
//    auto path_string = GeneratedFitsImagePath("2048 2048 1 4"); // 4 Stokes 
    std::string path_string = "/home/msok/github/CARTA/code/branches/issue1526/carta-backend/test/four_stokes.fits";
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);    
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
    
//    char szCmd[1024];
//    sprintf(szCmd,"cp %s /tmp/fits/",path_string.c_str());
//    system(szCmd);

    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    param_message.set_stokes_intensity(1);
    
    frame->SetVectorOverlayParameters( param_message );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          // std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;
          
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );

          // check angle tiles :
          const CARTA::TileData& tile_data = message.intensity_tiles(0);
          const std::string& image_data = tile_data.image_data();
          const float* float_data = (const float*)image_data.c_str(); // static_cast<const float*>(image_data.c_str());
          // std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          VectorField::CalcPi calcpi(0.0f,0.0f);
          float expected_pi = calcpi(2,3);
          // std::cout << "Expected PI = " << expected_pi << std::endl;
          int float_size = image_data.size()/4;
          for(int i=0;i<float_size;i++){
              EXPECT_NEAR( float_data[i] , expected_pi , 1e-8f);
          }                    
       };
       
    frame->CalculateVectorField( callback );   
}

