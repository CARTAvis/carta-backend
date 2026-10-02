/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "AnimationReadAhead.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <spdlog/spdlog.h>

namespace carta {

std::unique_ptr<AnimationReadAhead> AnimationReadAhead::For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files) {
    std::map<int, std::shared_ptr<PlaneReadAhead>> readable;
    std::uint64_t runs = 0;
    std::uint64_t cache = UINT64_MAX;
    for (const auto& [file_id, file] : files) {
        if (!file || file->RunBytes() == 0) {
            continue;
        }
        readable.emplace(file_id, file);
        runs += 2 * file->RunBytes();
        cache = std::min(cache, file->CacheBytes());
    }
    if (readable.empty()) {
        return nullptr;
    }
    if (runs > cache) {
        spdlog::debug("Animation reads nothing ahead: two runs of chunks of each file are {} MiB and the cache holds {} MiB",
            runs >> 20, cache >> 20);
        return nullptr;
    }
    return std::unique_ptr<AnimationReadAhead>(new AnimationReadAhead(std::move(readable)));
}

AnimationReadAhead::AnimationReadAhead(std::map<int, std::shared_ptr<PlaneReadAhead>> files) : _files(std::move(files)) {}

AnimationReadAhead::~AnimationReadAhead() {
    Cancel();
    Wait();
    spdlog::debug("Animation read {} runs of chunks ahead{}", _prefetches, _stopped ? ", then stopped for a late frame" : "");
}

bool AnimationReadAhead::UnderWay() const {
    return _under_way;
}

void AnimationReadAhead::Served(
    bool late, bool overlapped, const std::vector<AnimatedPlane>& shown, const std::vector<std::vector<AnimatedPlane>>& upcoming) {
    if (late && overlapped) {
        _stopped = true;
    }
    if (_stopped || _cancelled || _under_way) {
        return;
    }

    std::vector<AnimatedPlane> next;
    for (const auto& now : shown) {
        const auto file = _files.find(now.file_id);
        if (file == _files.end()) {
            continue;
        }
        const auto run_now = file->second->RunOf(now.z, now.stokes);
        for (const auto& frame : upcoming) {
            const auto plane = std::find_if(
                frame.begin(), frame.end(), [&](const AnimatedPlane& candidate) { return candidate.file_id == now.file_id; });
            if (plane == frame.end()) {
                continue;
            }
            const auto run = file->second->RunOf(plane->z, plane->stokes);
            if (run == run_now) {
                continue;
            }
            const auto requested = _requested.find(now.file_id);
            if (requested == _requested.end() || requested->second != run) {
                _requested[now.file_id] = run;
                next.push_back(*plane);
            }
            break;
        }
    }
    if (next.empty()) {
        return;
    }

    Wait();
    _under_way = true;
    _prefetches += static_cast<int>(next.size());
    // What the worker reads through is held for as long as it runs, so a file closed meanwhile is
    // still there to finish reading, or to be told to stop.
    std::vector<std::pair<std::shared_ptr<PlaneReadAhead>, AnimatedPlane>> work;
    for (const auto& plane : next) {
        work.emplace_back(_files.at(plane.file_id), plane);
    }
    _worker = std::thread([this, work = std::move(work)] {
        const auto cancelled = [this] { return _cancelled.load(); };
        for (const auto& [file, plane] : work) {
            if (_cancelled) {
                break;
            }
            file->Prefetch(plane.z, plane.stokes, cancelled);
        }
        _under_way = false;
    });
}

void AnimationReadAhead::Cancel() {
    _cancelled = true;
}

bool AnimationReadAhead::Stopped() const {
    return _stopped || _cancelled;
}

int AnimationReadAhead::Prefetches() const {
    return _prefetches;
}

void AnimationReadAhead::Wait() {
    if (_worker.joinable()) {
        _worker.join();
    }
}

} // namespace carta
