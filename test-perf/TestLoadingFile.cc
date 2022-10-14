/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <omp.h>

#include "CommonTestUtilities.h"
#include "Frame/VectorFieldCalculator.h"
#include "ImageData/FileLoader.h"
#include "TestFrame.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Timer/Timer.h"

using namespace carta;
using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 4) {
        spdlog::error(
            "Usage: "
            "./TestLoadingFile "
            "<full path name of the image file> "
            "<omp thread count, -1 means auto selected> "
            "<verbosity, 4: info, 5:debug> ");
        return 1;
    }

    // Set logger
    fs::path user_directory("");
    string verbosity_string(argv[3]);
    int verbosity = stoi(verbosity_string);
    logger::InitLogger(true, verbosity, true, false, user_directory);

    // Set image file name path
    string path_string(argv[1]);

    // Set OMP thread numbers
    string omp_thread_count_str(argv[2]);
    int omp_thread_count = stoi(omp_thread_count_str);
    if (omp_thread_count > 0) {
        omp_set_num_threads(omp_thread_count);
        spdlog::debug("OMP thread numbers {}", omp_thread_count);
    } else {
        omp_set_num_threads(omp_get_num_procs());
        spdlog::debug("OMP thread numbers {}", omp_get_num_procs());
    }

    // Set FileLoader
    carta::Timer t;
    shared_ptr<FileLoader> loader(FileLoader::GetLoader(path_string));
    spdlog::info("Elapsed time to load an image file for FileLoader: {} (us)", t.Elapsed().us());

    // Set Frame
    t.Reset();
    unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    spdlog::info("Elapsed time to load an image file for Frame: {} (us)", t.Elapsed().us());

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

    // Set required tiles
    int mip(2);
    std::vector<Tile> tiles;
    GetTiles(frame->Width(), frame->Height(), mip, tiles);
    int num_tiles = tiles.size();
    spdlog::debug("Number of tiles: {}", num_tiles);
    EXPECT_GT(num_tiles, 0);

    // Set required channel, stokes, compression type, and compression quality
    int z(0);
    int stokes(0);
    CARTA::CompressionType compression_type = CARTA::CompressionType::ZFP;
    float compression_quality(14.0);

    // Do raster tile data generation and compression
    t.Reset();
#pragma omp parallel
    {
        int num_threads = omp_get_num_threads();
        int stride = std::min(num_tiles, std::min(num_threads, MAX_TILING_TASKS));
#pragma omp for
        for (int j = 0; j < stride; j++) {
            for (int i = j; i < num_tiles; i += stride) {
                auto raster_tile_data = Message::RasterTileData(0, 0);
                auto tile = tiles[i];
                if (!frame->FillRasterTileData(raster_tile_data, tile, z, stokes, compression_type, compression_quality)) {
                    spdlog::warn("Discarding stale tile request for channel={}, layer={}, x={}, y={}", z, tile.layer, tile.x, tile.y);
                }
            }
        }
    }
    spdlog::info("Elapsed time to generate raster tile data: {} (us)", t.Elapsed().us());

    return 0;
}
