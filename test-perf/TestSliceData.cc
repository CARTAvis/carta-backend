/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <omp.h>

#include "AxesTransformer.h"
#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "TestFrame.h"
#include "Timer/Timer.h"
#include "Util/Casacore.h"

using namespace carta;
using namespace std;

static std::unordered_map<AxesTransformer::ViewAxis, std::string> view_axis_map = {
    {AxesTransformer::x, "[x-view]"}, {AxesTransformer::y, "[y-view]"}, {AxesTransformer::z, "[z-view]"}};

float GetReaderData(std::shared_ptr<DataReader> reader, const AxesTransformer::ViewAxis& view_axis, int i, int j, int channel, int stokes) {
    if (view_axis == AxesTransformer::z) {
        return reader->ReadPointXY(i, j, channel, stokes);
    } else if (view_axis == AxesTransformer::y) {
        return reader->ReadPointXY(i, channel, j, stokes);
    } else {
        return reader->ReadPointXY(channel, i, j, stokes);
    }
}

int main(int argc, char* argv[]) {
    // Set logger
    fs::path user_directory("");
    int verbosity(4);
    logger::InitLogger(true, verbosity, true, false, user_directory);

    if (argc != 4) {
        spdlog::error(
            "Usage: ./TestSliceData <full path name of the image file> <view axis: x, y, or z> <omp thread count, -1 means auto selected>");
        return 1;
    }

    // Set image file name path
    string file_path(argv[1]);
    string view_axis_str(argv[2]);

    // Set OMP thread numbers
    string omp_thread_count_str(argv[3]);
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
    auto frame = std::make_unique<TestFrame>(0, loader, "0");

    // Set view axis
    AxesTransformer::ViewAxis view_axis;
    if (view_axis_str == "x") {
        view_axis = AxesTransformer::x;
    } else if (view_axis_str == "y") {
        view_axis = AxesTransformer::y;
    } else {
        view_axis = AxesTransformer::z;
    }

    // Given the view axis z, get view width, height, and depth
    AxesTransformer transformer(frame->Width(), frame->Height(), frame->Depth(), view_axis);
    auto view_axes_sizes = transformer.GetViewAxesSizes();
    int width = view_axes_sizes["w"];
    int height = view_axes_sizes["h"];
    int depth = view_axes_sizes["d"];
    int channel = 0;
    int stokes = frame->CurrentStokes();

    // Get slice data from a cube image
    auto axes_ranges = transformer.GetAxesRanges(channel);
    double pixel_per_t = std::numeric_limits<double>::quiet_NaN();
    double dt = std::numeric_limits<double>::quiet_NaN();

    if ((CasacoreImageType(file_path) == casacore::ImageOpener::HDF5) &&
        (view_axis == AxesTransformer::x || view_axis == AxesTransformer::y)) {
        std::vector<float> image_data;

        Timer t;
        EXPECT_TRUE(frame->GetLoaderSwizzledData(image_data, stokes, axes_ranges["x"], axes_ranges["y"]));
        dt = t.Elapsed().us();

        EXPECT_TRUE(image_data.size() == width * height);
        spdlog::info("Use HDF5 swizzled data!");
    } else {
        StokesSlicer stokes_slicer = frame->GetImageSlicer(axes_ranges["x"], axes_ranges["y"], axes_ranges["z"], stokes);
        auto image_data_size = stokes_slicer.slicer.length().product();
        auto image_data = std::make_unique<float[]>(image_data_size);

        Timer t;
        EXPECT_TRUE(frame->GetSlicerData(stokes_slicer, image_data.get()));
        dt = t.Elapsed().us();

        EXPECT_TRUE((image_data_size == width * height));
    }

    pixel_per_t = width * height / dt;
    spdlog::info("{} {}x{} plane. Number of pixels per unit time: {:.3f} MPix/s", width, height, view_axis_map[view_axis], pixel_per_t);

    return 0;
}
