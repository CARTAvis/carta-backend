/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
#define CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <carta-zarr/read_ahead.h>

#include "ImageData/Zarr/PlaneReadAhead.h"

namespace carta {

// One plane of one file, as a frame of an animation shows it.
struct AnimatedPlane {
    int file_id = 0;
    int z = 0;
    int stokes = 0;
};

// Decodes the next run of chunks of each animated file while the frames of the current run play.
// Then the frame that enters the next run does not stall. carta-zarr's ReadAhead decides what to decode
// and when. This class translates CARTA's file ids, channels and stokes into carta-zarr's reads.
class AnimationReadAhead {
public:
    // How many upcoming frames to pass to Served. See carta::zarr::ReadAhead::kUpcomingFrames.
    static constexpr int UPCOMING_FRAMES = static_cast<int>(carta::zarr::ReadAhead::kUpcomingFrames);

    // Reads ahead for the files whose loader can read ahead. Returns null if no loader can, or if carta-zarr
    // declines. carta-zarr declines unless the cache holds two runs of every file that reads through it.
    static std::shared_ptr<AnimationReadAhead> For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files);

    // Stops the decoding under way and waits for it.
    ~AnimationReadAhead();
    AnimationReadAhead(const AnimationReadAhead&) = delete;
    AnimationReadAhead& operator=(const AnimationReadAhead&) = delete;

    // Called after each frame. The frame began at `began` and showed `shown`. It is late if it took longer
    // than the frame interval. `upcoming` has the planes of the next frames, nearest first.
    // Two frame tasks may call it at the same time, so a mutex lets only one call through at a time.
    // See carta::zarr::ReadAhead::Served.
    void Served(std::chrono::steady_clock::time_point began, bool late, const std::vector<AnimatedPlane>& shown,
        const std::vector<std::vector<AnimatedPlane>>& upcoming);

    // Stops the decoding under way without waiting for it, and starts no more.
    void Cancel();

    // Cancels `read_ahead` and drops this copy of it on a worker thread. The last copy waits for the decoding
    // thread when it goes, so a message handler must not drop it itself. Called when the animation stops,
    // a file closes or the session ends.
    static void Release(std::shared_ptr<AnimationReadAhead> read_ahead);

    carta::zarr::ReadAheadStats Stats() const;

private:
    AnimationReadAhead(
        std::map<int, std::shared_ptr<PlaneReadAhead>> files, std::map<int, std::size_t> images, carta::zarr::ReadAhead reading);
    // Converts `planes` to carta-zarr's planes. Skips a plane of a file that is not read ahead, and a plane that
    // its file does not have.
    std::vector<carta::zarr::AnimatedPlane> Library(const std::vector<AnimatedPlane>& planes) const;

    // Each file's loader, by file id. Holding them keeps the loaders alive.
    std::map<int, std::shared_ptr<PlaneReadAhead>> _files;
    // Each file's index in the images given to carta-zarr, by file id.
    std::map<int, std::size_t> _images;
    carta::zarr::ReadAhead _reading;
    // carta-zarr's Served must be called from one thread at a time.
    std::mutex _serving;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
