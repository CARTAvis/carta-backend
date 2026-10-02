/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
#define CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ImageData/PlaneReadAhead.h"

namespace carta {

// One plane of one file, as a frame of an animation shows it.
struct AnimatedPlane {
    int file_id = 0;
    int z = 0;
    int stokes = 0;
};

// Decoding the next run of chunks of each animated file while the frames of this one play.
//
// A frame that enters a run of chunks decodes all of it, which costs what jumping to the channel does,
// and every frame after it in the run is served from the cache in a few milliseconds. So the frame
// that enters a run stalls: measured on Lustre, a 7763 x 4742 cube in 512 x 512 x 4 chunks stalled
// for up to 135 ms every fourth frame at 5 frames a second, and in 512 x 512 x 16 chunks for up to
// 400 ms every sixteenth. Decoding the next run in the time the frames of this one leave over hid
// every stall for one viewer, at 5 and at 10 frames a second.
//
// Only while that time is to be had. One prefetch is under way at a time, and once a frame is late
// while one is, there are no more for the rest of the animation: the machine has no time over, and
// decoding ahead takes it from the frames being played. With eight viewers animating that cube at
// once, prefetches that kept ahead of every run still doubled the time of the frames played from the
// cache, and at 10 frames a second made more of them late however soon they stopped. A frame that
// catches a prefetch is not one of those: it waits for the decoding under way rather than starting
// its own.
//
// And only when the cache holds two runs of each file -- the one playing and the one decoded ahead --
// or the run decoded ahead evicts the one being played. A run is the plane rounded out to whole chunks
// times a chunk's depth: 589 MB for 512 x 512 x 4 chunks of that cube, 2.4 GB for 512 x 512 x 16.
class AnimationReadAhead {
public:
    // How many frames ahead the next run is looked for. A run deeper than this is decoded ahead once its
    // end is this near, which at CARTA's 5 frames a second is 13 s before it is needed.
    static constexpr int UPCOMING_FRAMES = 64;

    // Reads ahead for those of `files` that can be, or returns null when none can or their cache cannot
    // hold two runs of each of them.
    static std::unique_ptr<AnimationReadAhead> For(const std::map<int, std::shared_ptr<PlaneReadAhead>>& files);

    // Stops what is under way and waits for it.
    ~AnimationReadAhead();
    AnimationReadAhead(const AnimationReadAhead&) = delete;
    AnimationReadAhead& operator=(const AnimationReadAhead&) = delete;

    // Whether a prefetch is under way: asked before a frame is served, so that what is said of the frame
    // afterwards can say whether it shared the machine with one.
    bool UnderWay() const;

    // After a frame: `late` says it took longer than its turn and `overlapped` that a prefetch was under
    // way when it began, which together stop all reading ahead. Otherwise, with no prefetch under way,
    // starts one that decodes, for each file, the first run among `upcoming` -- the planes of the frames
    // to come, nearest first -- that is not the run of what it shows now in `shown`, unless that run has
    // been decoded ahead already.
    void Served(bool late, bool overlapped, const std::vector<AnimatedPlane>& shown,
        const std::vector<std::vector<AnimatedPlane>>& upcoming);

    // Stops what is under way without waiting for it, and starts nothing more. For an animation that
    // has stopped or whose file is closing.
    void Cancel();

    // Whether reading ahead stopped for a late frame or was cancelled.
    bool Stopped() const;
    // How many runs were decoded ahead, or started to be.
    int Prefetches() const;

private:
    explicit AnimationReadAhead(std::map<int, std::shared_ptr<PlaneReadAhead>> files);
    void Wait();

    std::map<int, std::shared_ptr<PlaneReadAhead>> _files;
    // The run last decoded ahead for each file, so that it is not decoded again.
    std::map<int, PlaneRun> _requested;
    std::thread _worker;
    std::atomic<bool> _under_way{false};
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _stopped{false};
    int _prefetches = 0;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_ANIMATIONREADAHEAD_H_
