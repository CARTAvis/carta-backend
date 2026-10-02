/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "Session/AnimationObject.h"
#include "Session/AnimationReadAhead.h"

using namespace carta;

namespace {

CARTA::AnimationFrame At(int channel, int stokes = 0) {
    CARTA::AnimationFrame frame;
    frame.set_channel(channel);
    frame.set_stokes(stokes);
    return frame;
}

// Channels 0 to 9 of Stokes 0, `delta` at a time.
AnimationObject Animation(int delta, bool looping, bool reverse_at_end, int start = 0) {
    auto start_frame = At(start);
    auto first = At(0);
    auto last = At(9);
    auto step = At(delta);
    google::protobuf::Map<google::protobuf::int32, CARTA::MatchedFrameList> matched;
    return AnimationObject(0, start_frame, first, last, step, matched, {}, 5, looping, reverse_at_end, true);
}

TEST(AnimationStep, AFrameStepsByTheDelta) {
    const auto animation = Animation(2, false, false);
    const auto step = animation.Step(At(3), true);
    EXPECT_EQ(step.frame.channel(), 5);
    EXPECT_TRUE(step.going_forward);
    EXPECT_TRUE(step.more);
    EXPECT_EQ(animation.Step(At(3), false).frame.channel(), 1);
}

TEST(AnimationStep, AtTheEndALoopStartsAgainAReversalTurnsAndOtherwiseItStops) {
    const auto looping = Animation(1, true, false).Step(At(9), true);
    EXPECT_EQ(looping.frame.channel(), 0);
    EXPECT_TRUE(looping.more);
    const auto backwards = Animation(1, true, false).Step(At(0), false);
    EXPECT_EQ(backwards.frame.channel(), 9);

    const auto reversing = Animation(1, false, true).Step(At(9), true);
    EXPECT_EQ(reversing.frame.channel(), 9) << "a reversal plays the frame it turned at again";
    EXPECT_FALSE(reversing.going_forward);
    EXPECT_TRUE(reversing.more);

    const auto ending = Animation(1, false, false).Step(At(9), true);
    EXPECT_FALSE(ending.more);
}

TEST(AnimationStep, WhatIsUpcomingIsWhatWillPlay) {
    // The next frame is the start frame until a frame has played.
    const auto frames = Animation(3, true, false, 6).Upcoming(4);
    std::vector<int> channels;
    for (const auto& frame : frames) {
        channels.push_back(frame.channel());
    }
    EXPECT_EQ(channels, (std::vector<int>{6, 9, 0, 3}));
    EXPECT_EQ(Animation(3, false, false, 6).Upcoming(4).size(), 2) << "an animation that stops has nothing after its end";
}

// Runs of `depth` channels, and a prefetch that a test can hold up until it lets go or the prefetch is
// cancelled.
class FakeReadAhead : public PlaneReadAhead {
public:
    explicit FakeReadAhead(int depth = 4, std::uint64_t run_bytes = 100, std::uint64_t cache_bytes = 1000)
        : _depth(depth), _run_bytes(run_bytes), _cache_bytes(cache_bytes) {}

    PlaneRun RunOf(int z, int stokes) const override {
        return {z / _depth, stokes};
    }
    std::uint64_t RunBytes() const override {
        return _run_bytes;
    }
    std::uint64_t CacheBytes() const override {
        return _cache_bytes;
    }
    bool Prefetch(int z, int stokes, const std::function<bool()>& cancelled) override {
        std::unique_lock<std::mutex> lock(_mutex);
        _prefetched.emplace_back(z, stokes);
        while (_holding && !cancelled()) {
            _released.wait_for(lock, std::chrono::milliseconds(1));
        }
        if (cancelled()) {
            _saw_cancel = true;
            return false;
        }
        return true;
    }

    void Hold() {
        std::scoped_lock lock(_mutex);
        _holding = true;
    }
    void Release() {
        {
            std::scoped_lock lock(_mutex);
            _holding = false;
        }
        _released.notify_all();
    }
    std::vector<std::pair<int, int>> Prefetched() {
        std::scoped_lock lock(_mutex);
        return _prefetched;
    }
    bool SawCancel() {
        std::scoped_lock lock(_mutex);
        return _saw_cancel;
    }

private:
    int _depth;
    std::uint64_t _run_bytes;
    std::uint64_t _cache_bytes;
    std::mutex _mutex;
    std::condition_variable _released;
    bool _holding = false;
    bool _saw_cancel = false;
    std::vector<std::pair<int, int>> _prefetched;
};

// The planes of file 0 at each channel in turn.
std::vector<std::vector<AnimatedPlane>> Upcoming(int from, int count, int file_id = 0) {
    std::vector<std::vector<AnimatedPlane>> frames;
    for (int z = from; z < from + count; ++z) {
        frames.push_back({{file_id, z, 0}});
    }
    return frames;
}

void WaitUntilIdle(const AnimationReadAhead& read_ahead) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (read_ahead.UnderWay() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(read_ahead.UnderWay()) << "a prefetch never finished";
}

TEST(AnimationReadAhead, NothingIsReadAheadWithoutRoomForTwoRunsOfEveryFile) {
    EXPECT_FALSE(AnimationReadAhead::For({{0, nullptr}})) << "a file whose loader cannot read ahead";
    EXPECT_FALSE(AnimationReadAhead::For({{0, std::make_shared<FakeReadAhead>(4, 600, 1000)}}));
    EXPECT_TRUE(AnimationReadAhead::For({{0, std::make_shared<FakeReadAhead>(4, 500, 1000)}}));
    EXPECT_FALSE(AnimationReadAhead::For(
        {{0, std::make_shared<FakeReadAhead>(4, 300, 1000)}, {1, std::make_shared<FakeReadAhead>(4, 300, 1000)}}))
        << "two files whose runs fit one at a time but not together";
}

TEST(AnimationReadAhead, TheNextRunIsDecodedOnceAndNoSooner) {
    auto file = std::make_shared<FakeReadAhead>();
    auto read_ahead = AnimationReadAhead::For({{0, file}});
    ASSERT_TRUE(read_ahead);

    read_ahead->Served(false, false, {{0, 0, 0}}, Upcoming(1, 2));
    WaitUntilIdle(*read_ahead);
    EXPECT_TRUE(file->Prefetched().empty()) << "a run was decoded before it came within reach";

    read_ahead->Served(false, false, {{0, 0, 0}}, Upcoming(1, 8));
    WaitUntilIdle(*read_ahead);
    read_ahead->Served(false, false, {{0, 1, 0}}, Upcoming(2, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(file->Prefetched(), (std::vector<std::pair<int, int>>{{4, 0}})) << "the next run, from its first plane, once";

    read_ahead->Served(false, false, {{0, 4, 0}}, Upcoming(5, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(file->Prefetched(), (std::vector<std::pair<int, int>>{{4, 0}, {8, 0}}));
    EXPECT_EQ(read_ahead->Prefetches(), 2);
}

TEST(AnimationReadAhead, OnePrefetchIsUnderWayAtATime) {
    auto file = std::make_shared<FakeReadAhead>();
    auto read_ahead = AnimationReadAhead::For({{0, file}});
    file->Hold();
    read_ahead->Served(false, false, {{0, 0, 0}}, Upcoming(1, 8));
    EXPECT_TRUE(read_ahead->UnderWay());
    read_ahead->Served(false, true, {{0, 4, 0}}, Upcoming(5, 8));
    file->Release();
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(file->Prefetched().size(), 1) << "a prefetch started while another was under way";

    read_ahead->Served(false, false, {{0, 5, 0}}, Upcoming(6, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(file->Prefetched().back(), std::make_pair(8, 0)) << "the run passed over was not decoded once there was room";
}

TEST(AnimationReadAhead, AFrameLateWhileAPrefetchIsUnderWayStopsReadingAhead) {
    auto file = std::make_shared<FakeReadAhead>();
    auto read_ahead = AnimationReadAhead::For({{0, file}});
    read_ahead->Served(true, false, {{0, 0, 0}}, Upcoming(1, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_FALSE(read_ahead->Stopped()) << "a frame late with nothing under way is no reason to stop";
    EXPECT_EQ(file->Prefetched().size(), 1);

    read_ahead->Served(true, true, {{0, 4, 0}}, Upcoming(5, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_TRUE(read_ahead->Stopped());
    read_ahead->Served(false, false, {{0, 8, 0}}, Upcoming(9, 8));
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(file->Prefetched().size(), 1) << "reading ahead went on after a frame was late beside it";
}

TEST(AnimationReadAhead, CancellingStopsWhatIsUnderWay) {
    auto file = std::make_shared<FakeReadAhead>();
    auto read_ahead = AnimationReadAhead::For({{0, file}});
    file->Hold();
    read_ahead->Served(false, false, {{0, 0, 0}}, Upcoming(1, 8));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (file->Prefetched().empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(file->Prefetched().size(), 1) << "the prefetch never started";
    read_ahead->Cancel();
    WaitUntilIdle(*read_ahead);
    EXPECT_TRUE(file->SawCancel());
    EXPECT_TRUE(read_ahead->Stopped());
}

TEST(AnimationReadAhead, EachAnimatedFileHasItsNextRunDecoded) {
    auto active = std::make_shared<FakeReadAhead>(4);
    auto matched = std::make_shared<FakeReadAhead>(2);
    auto read_ahead = AnimationReadAhead::For({{0, active}, {1, matched}});
    ASSERT_TRUE(read_ahead);
    std::vector<std::vector<AnimatedPlane>> upcoming;
    for (int z = 1; z < 8; ++z) {
        // The matched file moves at half the active one's pace.
        upcoming.push_back({{0, z, 0}, {1, z / 2, 1}});
    }
    read_ahead->Served(false, false, {{0, 0, 0}, {1, 0, 1}}, upcoming);
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(active->Prefetched(), (std::vector<std::pair<int, int>>{{4, 0}}));
    EXPECT_EQ(matched->Prefetched(), (std::vector<std::pair<int, int>>{{2, 1}}));
}

} // namespace
