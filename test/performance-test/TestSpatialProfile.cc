/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <benchmark/benchmark.h>

#include "BackendTester.h"
#include "CommonTestUtilities.h"

static void BM_SpatialProfile(benchmark::State& state, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(FileFinder::LargeImagePath(filename)), "0"));
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {GetSpatialConfig("x"), GetSpatialConfig("y")};

    frame->SetSpatialRequirements(profiles);
    frame->SetCursor(5, 5);
    std::string msg;
    frame->SetImageChannels(1, 0, msg);

    for (auto _ : state) {
        benchmark::DoNotOptimize(*frame);
        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);
        benchmark::ClobberMemory();
    }
}

static void BM_SpatialProfile_2(benchmark::State& state, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    // Set Protobuf messages
    CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);
    CARTA::CloseFile close_file = GetCloseFile(-1);
    CARTA::OpenFile open_file = GetOpenFile(FileFinder::LargeImagePath(""), filename, "0", 0, CARTA::RenderMode::RASTER);
    CARTA::SetImageChannels set_image_channels = GetSetImageChannels(0, 0, 0, CARTA::CompressionType::ZFP, 11);
    CARTA::SetCursor set_cursor = GetSetCursor(0, 5, 5);
    CARTA::SetSpatialRequirements set_spatial_requirements = GetSetSpatialRequirements(0, 0);

    // Create and set up the dummy backend
    std::unique_ptr<BackendModel> dummy_backend = BackendModel::GetDummyBackend();
    dummy_backend->Receive(register_viewer);
    dummy_backend->Receive(close_file);
    dummy_backend->Receive(open_file);
    dummy_backend->Receive(set_image_channels);
    dummy_backend->WaitForJobFinished();
    dummy_backend->Receive(set_cursor);
    dummy_backend->WaitForJobFinished();

    // Start benchmark tests
    for (auto _ : state) {
        benchmark::DoNotOptimize(*dummy_backend);
        dummy_backend->Receive(set_spatial_requirements);
        dummy_backend->WaitForJobFinished();
        benchmark::ClobberMemory();
    }
}

BENCHMARK_CAPTURE(BM_SpatialProfile, HDF5, "M17_SWex.hdf5")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile, CASA, "M17_SWex.image")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile, MIRIAD, "M17_SWex.miriad")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile, FITS, "M17_SWex.fits")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);

BENCHMARK_CAPTURE(BM_SpatialProfile_2, HDF5, "M17_SWex.hdf5")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile_2, CASA, "M17_SWex.image")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile_2, MIRIAD, "M17_SWex.miriad")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_SpatialProfile_2, FITS, "M17_SWex.fits")->MeasureProcessCPUTime()->UseRealTime()->Unit(benchmark::kMicrosecond);
