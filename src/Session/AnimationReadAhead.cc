/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "AnimationReadAhead.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <spdlog/spdlog.h>

#include "ThreadManager/ThreadManager.h"

namespace carta {

std::shared_ptr<AnimationReadAhead> AnimationReadAhead::For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files) {
    std::map<int, std::shared_ptr<PlaneReadAhead>> readable;
    std::map<int, std::size_t> images;
    std::vector<std::pair<carta::zarr::Image, carta::zarr::ReadOptions>> reads;
    for (const auto& [file_id, file] : files) {
        // Every plane of a file is read from the same image with the same options, so the first plane is enough.
        const auto plane = file ? file->Plane(0, 0) : std::nullopt;
        if (!plane) {
            continue;
        }
        readable.emplace(file_id, file);
        images.emplace(file_id, reads.size());
        reads.emplace_back(plane->image, plane->options);
    }
    if (reads.empty()) {
        return nullptr;
    }
    auto reading = carta::zarr::ReadAhead::For(reads);
    if (!reading) {
        // A cache too small for two runs is logged as info, because the operator can raise zarr_cache_size.
        // At the default size, a cube with chunks 16 channels deep does not fit.
        if (reading.error().code == carta::zarr::ErrorCode::buffer_too_small) {
            spdlog::info("Animation reads nothing ahead; raising zarr_cache_size would let it: {}", reading.error().message);
        } else {
            spdlog::debug("Animation reads nothing ahead: {}", reading.error().message);
        }
        return nullptr;
    }
    return std::shared_ptr<AnimationReadAhead>(new AnimationReadAhead(std::move(readable), std::move(images), std::move(reading).value()));
}

AnimationReadAhead::AnimationReadAhead(
    std::map<int, std::shared_ptr<PlaneReadAhead>> files, std::map<int, std::size_t> images, carta::zarr::ReadAhead reading)
    : _files(std::move(files)), _images(std::move(images)), _reading(std::move(reading)) {}

AnimationReadAhead::~AnimationReadAhead() {
    const auto stats = _reading.stats();
    // Releasing the carta-zarr ReadAhead cancels the decoding under way and waits for it.
    _reading.Cancel();
    spdlog::debug("Animation read {} runs of chunks ahead{}", stats.prefetches, stats.stopped ? ", then stopped" : "");
}

void AnimationReadAhead::Served(std::chrono::steady_clock::time_point began, bool late, const std::vector<AnimatedPlane>& shown,
    const std::vector<std::vector<AnimatedPlane>>& upcoming) {
    std::vector<std::vector<carta::zarr::AnimatedPlane>> ahead;
    ahead.reserve(std::min<std::size_t>(upcoming.size(), UPCOMING_FRAMES));
    for (const auto& frame : upcoming) {
        if (static_cast<int>(ahead.size()) == UPCOMING_FRAMES) {
            break;
        }
        ahead.push_back(Library(frame));
    }
    std::scoped_lock lock(_serving);
    _reading.Served(began, late, Library(shown), ahead);
}

void AnimationReadAhead::Cancel() {
    _reading.Cancel();
}

namespace {

// Drops a copy of a read-ahead on a worker thread.
class ReleaseTask : public Task {
public:
    explicit ReleaseTask(std::shared_ptr<AnimationReadAhead> read_ahead) : _read_ahead(std::move(read_ahead)) {}
    void execute() override {
        _read_ahead.reset();
    }

private:
    std::shared_ptr<AnimationReadAhead> _read_ahead;
};

} // namespace

void AnimationReadAhead::Release(std::shared_ptr<AnimationReadAhead> read_ahead) {
    if (!read_ahead) {
        return;
    }
    read_ahead->Cancel();
    ThreadManager::QueueTask(new ReleaseTask(std::move(read_ahead)));
}

carta::zarr::ReadAheadStats AnimationReadAhead::Stats() const {
    return _reading.stats();
}

std::vector<carta::zarr::AnimatedPlane> AnimationReadAhead::Library(const std::vector<AnimatedPlane>& planes) const {
    std::vector<carta::zarr::AnimatedPlane> library;
    library.reserve(planes.size());
    for (const auto& plane : planes) {
        const auto image = _images.find(plane.file_id);
        if (image == _images.end()) {
            continue;
        }
        if (auto read = _files.at(plane.file_id)->Plane(plane.z, plane.stokes)) {
            library.push_back({image->second, std::move(read->request)});
        }
    }
    return library;
}

} // namespace carta
