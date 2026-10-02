/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "AnimationReadAhead.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <spdlog/spdlog.h>

namespace carta {

std::unique_ptr<AnimationReadAhead> AnimationReadAhead::For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files) {
    std::map<int, std::shared_ptr<PlaneReadAhead>> readable;
    std::map<int, std::size_t> images;
    std::vector<std::pair<carta::zarr::Image, carta::zarr::ReadOptions>> reads;
    for (const auto& [file_id, file] : files) {
        // Every plane of a file is read from one image through the same options, so its first says
        // what all of them are read through.
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
        spdlog::debug("Animation reads nothing ahead: {}", reading.error().message);
        return nullptr;
    }
    return std::unique_ptr<AnimationReadAhead>(new AnimationReadAhead(std::move(readable), std::move(images), std::move(reading).value()));
}

AnimationReadAhead::AnimationReadAhead(
    std::map<int, std::shared_ptr<PlaneReadAhead>> files, std::map<int, std::size_t> images, carta::zarr::ReadAhead reading)
    : _files(std::move(files)), _images(std::move(images)), _reading(std::move(reading)) {}

AnimationReadAhead::~AnimationReadAhead() {
    const auto stats = _reading.stats();
    // What the library holds is let go of with this, which cancels the decode under way and waits.
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
    _reading.Served(began, late, Library(shown), ahead);
}

void AnimationReadAhead::Cancel() {
    _reading.Cancel();
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
