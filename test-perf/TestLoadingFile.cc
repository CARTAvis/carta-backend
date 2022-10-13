/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <omp.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "TestFrame.h"
#include "Timer/Timer.h"

using namespace carta;
using namespace std;

int main(int argc, char* argv[]) {
    // Set logger
    fs::path user_directory("");
    int verbosity(5);
    logger::InitLogger(true, verbosity, true, false, user_directory);

    if (argc != 3) {
        spdlog::error("Usage: ./TestLoadingFile <full path name of the image file> <omp thread count, -1 means auto selected>");
        return 1;
    }

    // Set image file name path
    string path_string(argv[1]);

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

    // Set file loader
    Timer t;
    shared_ptr<FileLoader> loader(FileLoader::GetLoader(path_string));
    spdlog::info("Elapsed time to load an image file for a loader: {} (us)", t.Elapsed().us());

    // Set Frame
    t.Reset();
    unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    spdlog::info("Elapsed time to load an image file for a frame: {} (us)", t.Elapsed().us());

    if (!frame->IsValid()) {
        spdlog::error("Failed to open the image file!");
        return 1;
    }

    // Calculate the image histogram
    auto region_histogram_data_callback = [&](CARTA::RegionHistogramData histogram_data) { EXPECT_TRUE(histogram_data.has_histograms()); };

    t.Reset();
    bool image_histogram = frame->FillRegionHistogramData(region_histogram_data_callback, IMAGE_REGION_ID, 0);
    spdlog::info("Elapsed time to calculate the image histogram: {} (us)", t.Elapsed().us());

    EXPECT_TRUE(image_histogram);

    return 0;
}
