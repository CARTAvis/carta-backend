/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <benchmark/benchmark.h>

#include "BackendTester.h"
#include "CommonTestUtilities.h"

static void BM_OpenFiles(benchmark::State& state, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    // Set Protobuf messages
    CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);
    CARTA::CloseFile close_file = GetCloseFile(-1);
    CARTA::OpenFile open_file = GetOpenFile(FileFinder::LargeImagePath(""), filename, "0", 0, CARTA::RenderMode::RASTER);

    // Create and set up the dummy backend
    std::unique_ptr<BackendModel> dummy_backend = BackendModel::GetDummyBackend();
    dummy_backend->Receive(register_viewer);
    dummy_backend->Receive(close_file);
    dummy_backend->ClearMessagesQueue();

    // Start benchmark tests
    for (auto _ : state) {
        benchmark::DoNotOptimize(*dummy_backend);
        dummy_backend->Receive(open_file);
        benchmark::ClobberMemory();
    }
}

BENCHMARK_CAPTURE(BM_OpenFiles, HDF5, "M17_SWex.hdf5")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, CASA, "M17_SWex.image")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, MIRIAD, "M17_SWex.miriad")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_OpenFiles, FITS, "M17_SWex.fits")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMillisecond);
