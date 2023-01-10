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

#define TEXT_COLOR "\033[1m\033[44m"
#define RESET "\033[0m\n"

using namespace carta;

class FitsImageTest2 : public ::testing::Test, public ImageGenerator {};

// Load a FITS image and get 2D slice data via the "fits_read_pix" function
int UsePureCfitsio(string file_path, vector<float>& image_data) {
    int status = 0;
    fitsfile* fptr;

    // Open image and get data size. File is assumed to be 2D
    fits_open_file(&fptr, file_path.c_str(), READONLY, &status);

    int dims = 4;
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
    auto message =
        fmt::format("[CFITSIO] For {}x{} plane, number of pixels per unit time: {:.3f} MPix/s", image_size[0], image_size[1], pixel_per_t);
    std::cout << TEXT_COLOR << message << RESET;

    fits_close_file(fptr, &status);

    GetVectorData(image_data, data_ptr, num_pixels);
    delete[] data_ptr;

    return status;
}

TEST_F(FitsImageTest2, BasicLoadingTest) {
    // Generate a FITS image
    auto file_path = GeneratedFitsImagePath("1000 1000 1 1");

    // Compress the FITS image
    string cmd = "gzip " + file_path;
    system(cmd.c_str());
    file_path += ".gz";

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
    EXPECT_TRUE(frame->GetSlicerData(stokes_slicer, image_data.get()));
    double dt = t.Elapsed().us();

    EXPECT_TRUE((image_data_size == width * height));

    double pixel_per_t = width * height / dt;
    auto message = fmt::format("[CARTA] For {}x{} plane, number of pixels per unit time: {:.3f} MPix/s", width, height, pixel_per_t);

    std::cout << TEXT_COLOR << message << RESET;

    // Check the consistency of image data getting by two ways
    vector<float> image_data1;
    GetVectorData(image_data1, image_data.get(), image_data_size);

    vector<float> image_data2;
    UsePureCfitsio(file_path, image_data2);

    CmpVectors(image_data1, image_data2);
}
