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

TEST_F(VectorFieldCalcTest, BasicStartTest) {
// TODO : how to generate images with 4 different values for IQUV - add option to make_image.py or create image myself ?
//    auto path_string = GeneratedFitsImagePath("2048 2048 1 4"); // 4 Stokes 
    std::string path_string = "~/github/CARTA/code/branches/issue1526/carta-backend/test/four_stokes.fits";
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);    
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
    
//    char szCmd[1024];
//    sprintf(szCmd,"cp %s /tmp/fits/",path_string.c_str());
//    system(szCmd);

//    std::string hdu{"/home/msok/github/CARTA/out.fits"};
//    std::string hdu{ path_string };
//    cout << "DEBUG : reading image " << hdu << std::endl;
//    TestFrame test_frame(1, loader, hdu );

    CARTA::SetVectorOverlayParameters param_message;
    param_message.set_smoothing_factor(2); // this was required as otherwise it returns on ClearParameters
    // TODO : add more set_ calls ???
    
    frame->SetVectorOverlayParameters( param_message );
    
    auto callback = [](CARTA::VectorOverlayTileData& message){
          std::cout << "DEBUG : received a message intensity tile size = " << message.intensity_tiles_size() << " , angle tile size = " << message.angle_tiles_size() << std::endl;
          
          // TODO : decode message and add to vector to be checked later :
          // Decode in Util/Message.h ??? but it takes string ...
          // How to get data values out of message ???
          // TileData : image_data()
          EXPECT_EQ( message.intensity_tiles_size(), 1 );
          EXPECT_EQ( message.angle_tiles_size(), 1 );
          const CARTA::TileData& tile_data = message.angle_tiles(0); // message.intensity_tiles(0);
          std::string image_data = tile_data.image_data();
          const float* float_data = (const float*)image_data.c_str(); // static_cast<const float*>(image_data.c_str());
          std::cout << "DEBUG : image_data len = " << image_data.length() << " test value = " << image_data[0] << " float values = " << float_data[0] << " , " << float_data[1] << std::endl; // not const value ??? even compressed in some way should be constant, right ?
          
          // vs: Compression.cc 
          // int Decompress(std::vector<float>& array, std::vector<char>& compression_buffer, int nx, int ny, int precision);
          std::vector<float> tile_data_float;
          std::vector<char>  tile_data_char( image_data.length() );
          memcpy(tile_data_char.data(),image_data.c_str(),image_data.length() );
          if(  Decompress( tile_data_float, tile_data_char, tile_data.width(), tile_data.height(), 100 ) ){
             std::cout << "WARNING : decompression failed !" << std::endl;
          }
          std::cout << "DEBUG2 : " << " char values = "  << tile_data_char[0] << " , " << tile_data_char[1] << std::endl;    // these have some values 
          std::cout << "DEBUG2 : " << " float values = " << tile_data_float[0] << " , " << tile_data_float[1] << std::endl;  // these seem to be all zeros ??? hmm...
          
// ???          
// QUESTION : how to decode these message to check if the values in IQUV images match 1,2,3,4 as in the generated image ???
// ???
 
// Both these crash with similar message :
//    unknown file: Failure
//     C++ exception with description "Failed to parse message" thrown in the test body.         
//
//          CARTA::TileData decoded_tile_data = Message::DecodeMessage<CARTA::TileData>( image_data );
//          CARTA::VectorOverlayTileData decoded_tile_data = Message::DecodeMessage<CARTA::VectorOverlayTileData>( image_data );
       };
       
    frame->CalculateVectorField( callback );   
}

