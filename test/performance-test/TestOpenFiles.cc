#include <benchmark/benchmark.h>
#include <tbb/task_scheduler_init.h>

#include "BackendTester.h"
#include "CommonTestUtilities.h"

std::mutex stats_mutex;

static void BMOpenFiles(benchmark::State& state, string filename) {
    // Check the existence of the sample image
    if (!FileExists(FileFinder::LargeImagePath(filename))) {
        return;
    }

    // Set threads number
    tbb::task_scheduler_init task_scheduler(TBB_TASK_THREAD_COUNT);
    omp_set_num_threads(omp_get_num_procs());
    spdlog::info("TBB task threads {}, OpenMP worker threads {}.", TBB_TASK_THREAD_COUNT, omp_get_num_procs());

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
        std::unique_lock<std::mutex> lock(stats_mutex);
        dummy_backend->Receive(close_file); // Close files
        dummy_backend->Receive(open_file);  // Open a file
        dummy_backend->ClearMessagesQueue();
    }
}

BENCHMARK_CAPTURE(BMOpenFiles, CASA, "M17_SWex.image")->Arg(10)->DenseThreadRange(1, 8)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BMOpenFiles, FITS, "M17_SWex.fits")->Arg(10)->DenseThreadRange(1, 8)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BMOpenFiles, MIRIAD, "M17_SWex.miriad")->Arg(10)->DenseThreadRange(1, 8)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
