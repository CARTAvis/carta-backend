/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// What each read path costs in wall time and peak memory, over any image the backend opens, so that
// a Zarr store can be compared with the FITS file it was made from. Skipped unless asked:
//
//   CARTA_MEASURE_FILE=/path/to/image.{zarr,fits} CARTA_MEASURE_OP=<op> \
//     ./carta_backend_tests --gtest_filter='MeasureReadPaths.One'
//
// One operation per process, so that nothing one of them leaves in a cache flatters the next. Every
// run opens the image first and reports that too, as op "open". Linux only: the peak is VmHWM, reset
// before each step through /proc/self/clear_refs.
//
// Ops: open, channels, cursor_spectrum, region_spectrum, image_histogram, cube_histogram_exact,
// cube_histogram_binned, cube_histogram_sampled, moment.
//
// Optional: CARTA_MEASURE_CHANNELS (channel changes for "channels", 8), CARTA_MEASURE_REGION_FRACTION
// (share of the plane a region box covers, 0.05), CARTA_MEASURE_CACHE_MB (the Zarr chunk cache, the
// server's default otherwise).

#include <gtest/gtest.h>
#include <omp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FileList/FileListHandler.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrContext.h"
#include "Main/ProgramSettings.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "Session/Session.h"
#include "Util/Message.h"

using namespace carta;

namespace {

std::string FromEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

// The process's resident set now and at its highest since the last reset, in KiB.
struct Resident {
    bool known = false;
    long long now_kib = 0;
    long long peak_kib = 0;
};

Resident ReadResident() {
    Resident resident;
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            status >> resident.now_kib;
            resident.known = true;
        } else if (key == "VmHWM:") {
            status >> resident.peak_kib;
        }
    }
    resident.known = resident.known && resident.peak_kib > 0;
    return resident;
}

// Brings VmHWM down to what is resident now, so the next peak is the next step's alone.
bool ResetPeak() {
    std::ofstream clear("/proc/self/clear_refs");
    clear << "5";
    clear.flush();
    return static_cast<bool>(clear);
}

// Runs `step`, reporting its wall time, its peak over what was resident before it, and what it
// left resident.
void MeasureStep(const std::string& file, const std::string& op, const std::function<bool()>& step) {
    const bool reset = ResetPeak();
    const auto before = ReadResident();
    const auto start = std::chrono::steady_clock::now();
    const bool ok = step();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto after = ReadResident();
    // A peak that could not be brought down, or not read, is no measurement of this step: say so
    // rather than print a number that looks like one.
    if (reset && before.known && after.known) {
        std::printf("measure: file=%s op=%s ok=%d seconds=%.3f peak_mib=%.0f peak_over_before_mib=%.0f after_mib=%.0f\n",
            std::filesystem::path(file).filename().c_str(), op.c_str(), ok ? 1 : 0, seconds, after.peak_kib / 1024.0,
            (after.peak_kib - before.now_kib) / 1024.0, after.now_kib / 1024.0);
    } else {
        std::printf("measure: file=%s op=%s ok=%d seconds=%.3f peak_mib=NA peak_over_before_mib=NA after_mib=NA\n",
            std::filesystem::path(file).filename().c_str(), op.c_str(), ok ? 1 : 0, seconds);
    }
    std::fflush(stdout);
    EXPECT_TRUE(ok) << op;
}

// A Session with no socket and no event loop, so every message it sends stays queued; see
// TestSession.cc.
class HeadlessSession : public Session {
public:
    HeadlessSession() : Session(nullptr, nullptr, 0, "", std::make_shared<FileListHandler>("/", ".")) {}

    void AdoptFrame(int file_id, std::shared_ptr<Frame> frame) {
        _frames[file_id] = std::move(frame);
    }

    // Whether the last queued histogram is a finished one, emptying the queue.
    bool FinishedHistogram() {
        bool finished = false;
        std::pair<std::vector<char>, bool> queued;
        while (_out_msgs.try_pop(queued)) {
            std::string whole(queued.first.data(), queued.first.size());
            if (Message::GetEventHeader(whole).GetType() == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                const auto data = Message::DecodeMessage<CARTA::RegionHistogramData>(whole);
                finished = data.progress() >= 1.0;
            }
        }
        return finished;
    }
};

std::shared_ptr<Frame> OpenFrame(const std::filesystem::path& path) {
    auto loader = FileLoader::GetLoader(path.string());
    if (!loader) {
        return nullptr;
    }
    std::string hdu = "0";
    if (std::filesystem::is_directory(path)) {
        hdu.clear();
        loader->OpenFile(hdu);
    }
    return std::make_shared<Frame>(0, std::move(loader), hdu);
}

// A box of `fraction` of the plane at its centre, as a polygon.
std::vector<CARTA::Point> CentredBox(Frame& f, double fraction) {
    const double side = std::sqrt(std::clamp(fraction, 0.0, 1.0));
    const double w = f.Width() * side;
    const double h = f.Height() * side;
    const double x0 = (f.Width() - w) / 2.0;
    const double y0 = (f.Height() - h) / 2.0;
    return {Message::Point(x0, y0), Message::Point(x0 + w, y0), Message::Point(x0 + w, y0 + h), Message::Point(x0, y0 + h)};
}

bool RunCubeHistogram(const std::string& method, const std::shared_ptr<Frame>& frame) {
    ProgramSettings::GetInstance().zarr_histogram_method = method;
    HeadlessSession session;
    session.AdoptFrame(0, frame);
    CARTA::SetHistogramRequirements message;
    message.set_file_id(0);
    message.set_region_id(CUBE_REGION_ID);
    auto* config = message.add_histograms();
    config->set_coordinate("z");
    config->set_channel(ALL_Z);
    config->set_num_bins(AUTO_BIN_SIZE);
    session.OnSetHistogramRequirements(message, 1);
    return session.FinishedHistogram();
}

} // namespace

TEST(MeasureReadPaths, One) {
    const std::string file = FromEnv("CARTA_MEASURE_FILE");
    const std::string op = FromEnv("CARTA_MEASURE_OP");
    if (file.empty() || op.empty()) {
        GTEST_SKIP() << "set CARTA_MEASURE_FILE and CARTA_MEASURE_OP to measure";
    }

    // As Main.cc configures the server, so the Zarr chunk cache is the one it runs with.
    const std::string cache = FromEnv("CARTA_MEASURE_CACHE_MB");
    ConfigureZarrContext(
        ZARR_FILE_IO_CONCURRENCY, ZARR_DATA_COPY_CONCURRENCY, cache.empty() ? ZARR_CACHE_POOL_MB : std::stoi(cache), omp_get_num_procs());
    spdlog::set_level(spdlog::level::warn);

    std::shared_ptr<Frame> frame;
    MeasureStep(file, "open", [&] {
        frame = OpenFrame(file);
        return frame && frame->IsValid();
    });
    ASSERT_TRUE(frame && frame->IsValid());
    const int depth = static_cast<int>(frame->Depth());

    if (op == "open") {
        return;
    }
    if (op == "channels") {
        const std::string count_env = FromEnv("CARTA_MEASURE_CHANNELS");
        const int count = std::min(depth - 1, count_env.empty() ? 8 : std::stoi(count_env));
        MeasureStep(file, op, [&] {
            std::string message;
            for (int z = 1; z <= count; ++z) {
                if (!frame->SetImageChannels(z, 0, message)) {
                    return false;
                }
            }
            return true;
        });
    } else if (op == "cursor_spectrum") {
        MeasureStep(file, op, [&] {
            frame->SetCursor(frame->Width() / 2.0f, frame->Height() / 2.0f);
            CARTA::SetSpectralRequirements_SpectralConfig config;
            config.set_coordinate("z");
            config.add_stats_types(CARTA::StatsType::Sum);
            if (!frame->SetSpectralRequirements(CURSOR_REGION_ID, {config})) {
                return false;
            }
            bool complete = false;
            frame->FillSpectralProfileData(
                [&](CARTA::SpectralProfileData data) { complete = complete || data.progress() >= 1.0; }, CURSOR_REGION_ID, false);
            return complete;
        });
    } else if (op == "region_spectrum") {
        const std::string fraction_env = FromEnv("CARTA_MEASURE_REGION_FRACTION");
        const double fraction = fraction_env.empty() ? 0.05 : std::stod(fraction_env);
        MeasureStep(file, op, [&] {
            carta::RegionHandler region_handler;
            int region_id = -1;
            RegionState state(0, CARTA::POLYGON, CentredBox(*frame, fraction), 0.0);
            if (!region_handler.SetRegion(region_id, state, frame->CoordinateSystem())) {
                return false;
            }
            CARTA::SetSpectralRequirements_SpectralConfig config;
            config.set_coordinate("z");
            for (auto stat : {CARTA::StatsType::NumPixels, CARTA::StatsType::Sum, CARTA::StatsType::Mean, CARTA::StatsType::RMS,
                     CARTA::StatsType::Sigma, CARTA::StatsType::Min, CARTA::StatsType::Max}) {
                config.add_stats_types(stat);
            }
            if (!region_handler.SetSpectralRequirements(region_id, 0, frame, {config})) {
                return false;
            }
            bool complete = false;
            region_handler.FillSpectralProfileData(
                [&](CARTA::SpectralProfileData data) { complete = complete || data.progress() >= 1.0; }, region_id, 0, false);
            return complete;
        });
    } else if (op == "image_histogram") {
        MeasureStep(file, op, [&] {
            // A channel that is not the current one, so the plane has to be read.
            std::string message;
            if (depth > 1 && !frame->SetImageChannels(depth / 2, 0, message)) {
                return false;
            }
            CARTA::HistogramConfig config;
            config.set_coordinate("z");
            config.set_channel(CURRENT_Z);
            config.set_num_bins(AUTO_BIN_SIZE);
            if (!frame->SetHistogramRequirements(IMAGE_REGION_ID, {config})) {
                return false;
            }
            return frame->FillRegionHistogramData([](CARTA::RegionHistogramData) {}, IMAGE_REGION_ID, 0, true);
        });
    } else if (op == "cube_histogram_exact" || op == "cube_histogram_binned" || op == "cube_histogram_sampled") {
        const std::string method = op.substr(std::string("cube_histogram_").size());
        MeasureStep(file, op, [&] { return RunCubeHistogram(method, frame); });
    } else if (op == "moment") {
        MeasureStep(file, op, [&] {
            CARTA::MomentRequest request;
            request.set_file_id(0);
            request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
            request.set_axis(CARTA::MomentAxis::SPECTRAL);
            request.set_region_id(0);
            request.mutable_spectral_range()->set_min(0);
            request.mutable_spectral_range()->set_max(depth - 1);
            request.set_mask(CARTA::MomentMask::None);
            StokesRegion stokes_region;
            if (!frame->GetImageRegion(0, AxisRange(0, depth - 1), frame->CurrentStokes(), stokes_region)) {
                return false;
            }
            CARTA::MomentResponse response;
            std::vector<GeneratedImage> results;
            frame->CalculateMoments(
                0, [](float) {}, stokes_region, request, response, results);
            return response.success() && results.size() == 1;
        });
    } else {
        FAIL() << "unknown CARTA_MEASURE_OP " << op;
    }
}
