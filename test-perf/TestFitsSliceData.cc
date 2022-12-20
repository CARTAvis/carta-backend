/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <fitsio.h>
#include <omp.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "Timer/Timer.h"
#include "Util/Casacore.h"

using namespace carta;
using namespace std;

int UseCfitsio(string file_path, vector<float>& image_data) {
    int status = 0;
    fitsfile* fptr;
    int dims = 4;
    std::vector<long> image_size(dims);
    std::vector<float> data_vector;
    float* data_ptr = nullptr;

    std::vector<long> start_pix(dims);
    for (int i = 0; i < dims; i++) {
        start_pix[i] = 1;
    }

    // Open image and get data size. File is assumed to be 2D
    fits_open_file(&fptr, file_path.c_str(), READONLY, &status);
    fits_get_img_size(fptr, dims, image_size.data(), &status);
    int64_t num_pixels = image_size[0] * image_size[1];
    float test_val = 0;

    // Frame::FillImageCache timing is just the memory allocation and reading part
    Timer t;
    data_ptr = new float[num_pixels];

    fits_read_pix(fptr, TFLOAT, start_pix.data(), num_pixels, nullptr, data_ptr, nullptr, &status);
    test_val = data_ptr[num_pixels / 2];
    auto t_end = std::chrono::high_resolution_clock::now();

    auto dt = t.Elapsed().us();
    double pixel_per_t = (double)image_size[0] * (double)image_size[1] / dt;
    spdlog::info("{}x{} plane. [cfitsio] Number of pixels per unit time: {:.3f} MPix/s", image_size[0], image_size[1], pixel_per_t);

    fits_close_file(fptr, &status);

    GetVectorData(image_data, data_ptr, num_pixels);
    delete[] data_ptr;

    return status;
}

int main(int argc, char* argv[]) {
    // Set logger
    fs::path user_directory("");
    int verbosity(4);
    logger::InitLogger(true, verbosity, true, false, user_directory);

    if (argc != 3) {
        spdlog::error("Usage: ./TestFitsSliceData <full path name of the image file> <omp thread count, -1 means auto selected>");
        return 1;
    }

    // Set image file name path
    string file_path(argv[1]);

    // Set OMP thread numbers
    string omp_thread_count_str(argv[2]);
    int omp_thread_count = stoi(omp_thread_count_str);
    if (omp_thread_count > 0) {
        omp_set_num_threads(omp_thread_count);
        spdlog::info("OMP thread numbers {}", omp_thread_count);
    } else {
        omp_set_num_threads(omp_get_num_procs());
        spdlog::info("OMP thread numbers {}", omp_get_num_procs());
    }

    // Load a cube image file
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
    auto frame = std::make_unique<Frame>(0, loader, "0");

    int width = frame->Width();
    int height = frame->Height();
    int channel = 0;
    int stokes = frame->CurrentStokes();

    double pixel_per_t = std::numeric_limits<double>::quiet_NaN();
    double dt = std::numeric_limits<double>::quiet_NaN();

    StokesSlicer stokes_slicer = frame->GetImageSlicer(AxisRange(0, width - 1), AxisRange(0, height - 1), AxisRange(channel), stokes);
    auto image_data_size = stokes_slicer.slicer.length().product();
    auto image_data = std::make_unique<float[]>(image_data_size);

    Timer t;
    EXPECT_TRUE(frame->GetSlicerData(stokes_slicer, image_data.get()));
    dt = t.Elapsed().us();

    EXPECT_TRUE((image_data_size == width * height));

    pixel_per_t = width * height / dt;
    spdlog::info("{}x{} plane. [carta] Number of pixels per unit time: {:.3f} MPix/s", width, height, pixel_per_t);

    // Check the consistency of image data getting by two ways
    vector<float> image_data1;
    GetVectorData(image_data1, image_data.get(), image_data_size);
    vector<float> image_data2;
    UseCfitsio(file_path, image_data2);
    CmpVectors(image_data1, image_data2);

    return 0;
}
