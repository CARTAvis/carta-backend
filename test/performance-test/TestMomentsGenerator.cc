/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <benchmark/benchmark.h>

#include "BackendTester.h"
#include "CommonTestUtilities.h"

static void BM_MomentsGenerator(benchmark::State& state, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    auto register_viewer = GetRegisterViewer(0, "", 5);
    auto close_file = GetCloseFile(-1);
    auto open_file = GetOpenFile(FileFinder::LargeImagePath(""), filename, "0", 0, CARTA::RenderMode::RASTER);
    auto moment_request =
        GetMomentsRequest(0, 0, CARTA::MomentAxis::SPECTRAL, CARTA::MomentMask::Include, GetIntBounds(0, 24), GetFloatBounds(-1, 1));

    // Create a dummy backend and execute commands
    std::unique_ptr<BackendModel> dummy_backend = BackendModel::GetDummyBackend();
    dummy_backend->Receive(register_viewer);
    dummy_backend->Receive(close_file);
    dummy_backend->Receive(open_file);
    dummy_backend->ClearMessagesQueue();

    // Start benchmark tests
    for (auto _ : state) {
        benchmark::DoNotOptimize(*dummy_backend);
        dummy_backend->Receive(moment_request);
        dummy_backend->WaitForJobFinished();
        benchmark::ClobberMemory();
    }
}

BENCHMARK_CAPTURE(BM_MomentsGenerator, HDF5, "M17_SWex.hdf5")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kSecond);
BENCHMARK_CAPTURE(BM_MomentsGenerator, CASA, "M17_SWex.image")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kSecond);
BENCHMARK_CAPTURE(BM_MomentsGenerator, MIRIAD, "M17_SWex.miriad")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kSecond);
BENCHMARK_CAPTURE(BM_MomentsGenerator, FITS, "M17_SWex.fits")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kSecond);
