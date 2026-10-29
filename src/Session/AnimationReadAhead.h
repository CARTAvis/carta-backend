/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
#define CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_

#include <chrono>
#include <map>
#include <memory>
#include <vector>

#include <carta-zarr/read_ahead.h>

#include "ImageData/PlaneReadAhead.h"

namespace carta {

// One plane of one file, as a frame of an animation shows it.
struct AnimatedPlane {
    int file_id = 0;
    int z = 0;
    int stokes = 0;
};

// Decoding the next run of chunks of each animated file while the frames of this one play, so that the
// frame entering it does not stall. What is decoded and when is carta-zarr's ReadAhead -- its header and
// ADR 0016 have the measurements behind it -- and this says it in CARTA's terms: files by id, planes by
// channel and Stokes, through each file's loader.
class AnimationReadAhead {
public:
    // How many frames ahead the next run is looked for, and so how many of them to say. See
    // carta::zarr::ReadAhead::kUpcomingFrames.
    static constexpr int UPCOMING_FRAMES = static_cast<int>(carta::zarr::ReadAhead::kUpcomingFrames);

    // Reads ahead for those of `files` whose loader can, or returns null when none can or the library
    // declines -- as it does unless each cache they read through holds two runs of every file in it.
    static std::unique_ptr<AnimationReadAhead> For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files);

    // Stops what is under way and waits for it.
    ~AnimationReadAhead();
    AnimationReadAhead(const AnimationReadAhead&) = delete;
    AnimationReadAhead& operator=(const AnimationReadAhead&) = delete;

    // After a frame that began at `began` and showed `shown`, late or not by the animation's own frame
    // interval; `upcoming` is the planes of the frames to come, nearest first. See
    // carta::zarr::ReadAhead::Served.
    void Served(std::chrono::steady_clock::time_point began, bool late, const std::vector<AnimatedPlane>& shown,
        const std::vector<std::vector<AnimatedPlane>>& upcoming);

    // Stops what is under way without waiting for it, and starts nothing more. For an animation that
    // has stopped or whose file is closing.
    void Cancel();

    carta::zarr::ReadAheadStats Stats() const;

private:
    AnimationReadAhead(
        std::map<int, std::shared_ptr<PlaneReadAhead>> files, std::map<int, std::size_t> images, carta::zarr::ReadAhead reading);
    // `planes` as the library's: the read of each, and its file's place among the images reading ahead
    // was made for. A plane of a file not read ahead, or one its file does not have, is left out.
    std::vector<carta::zarr::AnimatedPlane> Library(const std::vector<AnimatedPlane>& planes) const;

    // Held for as long as reading ahead asks them how a plane is read.
    std::map<int, std::shared_ptr<PlaneReadAhead>> _files;
    std::map<int, std::size_t> _images;
    carta::zarr::ReadAhead _reading;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
