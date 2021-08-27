/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <benchmark/benchmark.h>
#include <tbb/task_scheduler_init.h>

#include "BackendTester.h"
#include "CommonTestUtilities.h"

static void BM_OpenFiles(benchmark::State& state, int omp_thread_count, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    // Set omp threads number
    if (omp_thread_count < 1) {
        omp_thread_count = omp_get_num_procs();
    }
    tbb::task_scheduler_init task_scheduler(TBB_TASK_THREAD_COUNT);
    omp_set_num_threads(omp_thread_count);

    // Set spdlog level
    spdlog::default_logger()->set_level(spdlog::level::err);

    // Define Protobuf messages
    CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);
    CARTA::CloseFile close_file = GetCloseFile(-1);
    CARTA::OpenFile open_file = GetOpenFile(FileFinder::LargeImagePath(""), filename, "0", 0, CARTA::RenderMode::RASTER);

    // Create a dummy backend
    std::unique_ptr<BackendModel> dummy_backend = BackendModel::GetDummyBackend();

    // Register the viewer for the dummy backend
    dummy_backend->Receive(register_viewer);
    dummy_backend->ClearMessagesQueue();

    // Start the benchmark test
    for (auto _ : state) {
        benchmark::DoNotOptimize(*dummy_backend); // is it necessary?
        dummy_backend->Receive(close_file);
        dummy_backend->Receive(open_file);
        dummy_backend->ClearMessagesQueue();
        benchmark::ClobberMemory(); // is it necessary?
    }
}

BENCHMARK_CAPTURE(BM_OpenFiles, CASA_1, 1, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_2, 2, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_3, 3, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_4, 4, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_5, 5, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_6, 6, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_7, 7, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA_8, 8, "M17_SWex.image")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_OpenFiles, FITS_1, 1, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_2, 2, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_3, 3, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_4, 4, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_5, 5, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_6, 6, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_7, 7, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS_8, 8, "M17_SWex.fits")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_1, 1, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_2, 2, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_3, 3, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_4, 4, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_5, 5, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_6, 6, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_7, 7, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, HDF5_8, 8, "M17_SWex.hdf5")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);

BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_1, 1, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_2, 2, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_3, 3, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_4, 4, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_5, 5, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_6, 6, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_7, 7, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD_8, 8, "M17_SWex.miriad")->MeasureProcessCPUTime()->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
