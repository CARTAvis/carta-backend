/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
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

// What is decoded ahead and when is carta-zarr's ReadAhead, and its tests say so; what is left here is
// saying it in CARTA's terms. The pixel fixture has two channels and three Stokes, each plane a chunk deep
// in both, so every plane is a run of its own.
const std::uint64_t kCacheBytes = std::uint64_t(64) << 20;

struct Loaded {
    std::shared_ptr<FileLoader> loader;
    std::shared_ptr<void> cache;
    std::shared_ptr<PlaneReadAhead> read_ahead;
};

// The fixture through a loader of its own, whose reads keep what they decode in a cache of `bytes` of
// their own, so that the test does not size the process's.
Loaded Load(std::uint64_t bytes = kCacheBytes) {
    Loaded loaded;
    loaded.loader = FileLoader::GetLoader(ZARR_PIXEL_FIXTURE);
    loaded.loader->OpenFile("");
    auto* image = dynamic_cast<CartaZarrImage*>(loaded.loader->GetImage().get());
    loaded.cache = image->OwnCache()(bytes);
    loaded.read_ahead = std::shared_ptr<PlaneReadAhead>(loaded.loader, loaded.loader->ReadAhead());
    return loaded;
}

void WaitUntilIdle(const AnimationReadAhead& read_ahead) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (read_ahead.Stats().under_way && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(read_ahead.Stats().under_way) << "a prefetch never finished";
}

TEST(AnimationReadAhead, AFileWhoseLoaderCannotReadAheadIsLeftOut) {
    EXPECT_FALSE(AnimationReadAhead::For({{0, nullptr}}));
    const auto loaded = Load();
    ASSERT_TRUE(loaded.read_ahead->Plane(0, 0).has_value());
    EXPECT_TRUE(AnimationReadAhead::For({{0, nullptr}, {1, loaded.read_ahead}})) << "the file that can was left out too";
}

TEST(AnimationReadAhead, TheCacheAskedIsTheOneTheFramesReadThrough) {
    EXPECT_FALSE(AnimationReadAhead::For({{0, Load(1).read_ahead}})) << "a file's own cache of a byte was read ahead into";
    EXPECT_TRUE(AnimationReadAhead::For({{0, Load().read_ahead}}));
}

TEST(AnimationReadAhead, EachFileHasTheNextRunOfItsOwnPlanesDecoded) {
    const auto active = Load();
    const auto matched = Load();
    auto read_ahead = AnimationReadAhead::For({{3, active.read_ahead}, {8, matched.read_ahead}});
    ASSERT_TRUE(read_ahead);
    EXPECT_FALSE(active.read_ahead->Plane(0, 3).has_value()) << "a Stokes the file does not have has a plane";

    // A file not animated, and a Stokes the file does not have, are passed over.
    read_ahead->Served(
        std::chrono::steady_clock::now(), false, {{3, 0, 0}, {8, 0, 1}, {5, 0, 0}}, {{{3, 1, 0}, {8, 0, 3}}, {{3, 1, 0}, {8, 1, 1}}});
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(read_ahead->Stats().prefetches, 2) << "the next run of each animated file was not decoded, once";
}

} // namespace
