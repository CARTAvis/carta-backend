/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "Timer/Timer.h"

using namespace carta;

// Load a FITS image and get 2D slice data via the "fits_read_pix" function
double UsePureCfitsio(string file_path, int dims, vector<float>& image_data) {
    int status = 0;
    fitsfile* fptr;

    // Open image and get data size. File is assumed to be 2D
    fits_open_file(&fptr, file_path.c_str(), READONLY, &status);

    std::vector<long> image_size(dims);
    std::vector<long> start_pix(dims);

    for (int i = 0; i < dims; i++) {
        start_pix[i] = 1;
    }

    fits_get_img_size(fptr, dims, image_size.data(), &status);
    int64_t num_pixels = image_size[0] * image_size[1];

    // Check the elapsed time to get a 2D slice data
    Timer t;

    float* data_ptr = new float[num_pixels];
    fits_read_pix(fptr, TFLOAT, start_pix.data(), num_pixels, nullptr, data_ptr, nullptr, &status);

    double dt = t.Elapsed().us();

    double pixel_per_t = (double)image_size[0] * (double)image_size[1] / dt;
    auto message = fmt::format(
        "[CFITSIO] For {}x{} image data, number of pixels per unit time: {:.3f} MPix/s", image_size[0], image_size[1], pixel_per_t);
    std::cout << message << endl;

    fits_close_file(fptr, &status);

    GetVectorData(image_data, data_ptr, num_pixels);
    delete[] data_ptr;

    return pixel_per_t;
}

class FitsImageTest2 : public ::testing::Test, public ImageGenerator {
public:
    static void Load2DSliceData(const vector<int>& shape, bool compressed) {
        // Generate a FITS image
        string image_shape;
        for (int i = 0; i < shape.size(); ++i) {
            image_shape += to_string(shape[i]);
            if (i != shape.size() - 1) {
                image_shape += ' ';
            }
        }
        auto file_path = GeneratedFitsImagePath(image_shape);

        if (compressed) {
            // Compress the FITS image
            string cmd = "gzip " + file_path;
            system(cmd.c_str());
            file_path += ".gz";
        }

        // Load a compressed FITS image
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        auto frame = std::make_unique<Frame>(0, loader, "0");

        int width = frame->Width();
        int height = frame->Height();
        int channel = 0;
        int stokes = frame->CurrentStokes();

        StokesSlicer stokes_slicer = frame->GetImageSlicer(AxisRange(0, width - 1), AxisRange(0, height - 1), AxisRange(channel), stokes);
        auto image_data_size = stokes_slicer.slicer.length().product();
        auto image_data = std::make_unique<float[]>(image_data_size);

        // Check the elapsed time to get a 2D slice data
        Timer t;
        frame->GetSlicerData(stokes_slicer, image_data.get());
        double dt = t.Elapsed().us();

        EXPECT_TRUE((image_data_size == width * height));

        double pixel_per_t = width * height / dt;
        auto message =
            fmt::format("[CARTA] For {}x{} image data, number of pixels per unit time: {:.3f} MPix/s", width, height, pixel_per_t);

        std::cout << message << endl;

        // Check the consistency of image data getting by two ways
        vector<float> image_data1;
        GetVectorData(image_data1, image_data.get(), image_data_size);

        // Get 2D slice data with pure cfitsio
        vector<float> image_data2;
        double pixel_per_t2 = UsePureCfitsio(file_path, shape.size(), image_data2);

        CmpVectors(image_data1, image_data2);

        auto message2 = fmt::format("Compare the performances [CARTA]/[CFITSIO] = {:.3f}", pixel_per_t / pixel_per_t2);
        std::cout << message2 << endl;
    }
};

TEST_F(FitsImageTest2, LoadCompressed2DSliceData) {
    Load2DSliceData({500, 500, 1, 1}, true);
    Load2DSliceData({1000, 1000, 1, 1}, true);
    Load2DSliceData({2000, 2000, 1, 1}, true);
}

TEST_F(FitsImageTest2, Load2DSliceData) {
    Load2DSliceData({500, 500, 1, 1}, false);
    Load2DSliceData({1000, 1000, 1, 1}, false);
    Load2DSliceData({2000, 2000, 1, 1}, false);
}

TEST_F(FitsImageTest2, CompressedFitsSpatialProfile) {
    auto path_string = GeneratedFitsImagePath("100 100");
    FitsDataReader reader(path_string);
    auto ref_point = reader.ReadPointXY(5, 5);
    auto ref_profile_x = reader.ReadProfileX(5);
    auto ref_profile_y = reader.ReadProfileY(5);

    // Compress the FITS file
    string cmd = "gzip " + path_string;
    system(cmd.c_str());
    string gzip_path_string = path_string + ".gz";

    // Open compressed FITS file through the Frame loader
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(gzip_path_string));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Get spectral profiles from the compressed FITS file
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
    frame->SetSpatialRequirements(profiles);
    frame->SetCursor(5, 5);

    std::vector<CARTA::SpatialProfileData> data_vec;
    frame->FillSpatialProfileData(data_vec);

    // Check with the spatial profiles from normal (uncompressed) FITS file
    for (auto& data : data_vec) {
        EXPECT_EQ(data.file_id(), 0);
        EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
        EXPECT_EQ(data.x(), 5);
        EXPECT_EQ(data.y(), 5);
        EXPECT_EQ(data.channel(), 0);
        EXPECT_EQ(data.stokes(), 0);
        CmpValues(data.value(), ref_point);
        EXPECT_EQ(data.profiles_size(), 2);

        auto [x_profile, y_profile] = GetProfiles(data);

        EXPECT_EQ(x_profile.start(), 0);
        EXPECT_EQ(x_profile.end(), 100);
        EXPECT_EQ(x_profile.mip(), 0);
        auto x_vals = GetSpatialProfileValues(x_profile);
        EXPECT_EQ(x_vals.size(), 100);
        CmpVectors(x_vals, ref_profile_x);

        EXPECT_EQ(y_profile.start(), 0);
        EXPECT_EQ(y_profile.end(), 100);
        EXPECT_EQ(y_profile.mip(), 0);
        auto y_vals = GetSpatialProfileValues(y_profile);
        EXPECT_EQ(y_vals.size(), 100);
        CmpVectors(y_vals, ref_profile_y);
    }
}
