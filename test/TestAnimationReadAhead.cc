/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "ImageData/Zarr/ZarrContext.h"
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

// Channels 0 to 9 of stokes 0, `delta` channels at a time.
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

TEST(AnimationStep, AtAnEndALoopStartsAgainAReversalTurnsAndOtherwiseItStops) {
    const auto looping = Animation(1, true, false).Step(At(9), true);
    EXPECT_EQ(looping.frame.channel(), 0);
    EXPECT_TRUE(looping.more);
    EXPECT_EQ(Animation(1, true, false).Step(At(0), false).frame.channel(), 9);

    const auto reversing = Animation(1, false, true).Step(At(9), true);
    EXPECT_EQ(reversing.frame.channel(), 9) << "a reversal plays the end frame again";
    EXPECT_FALSE(reversing.going_forward);
    EXPECT_TRUE(reversing.more);

    EXPECT_FALSE(Animation(1, false, false).Step(At(9), true).more);
}

TEST(AnimationStep, UpcomingFramesAreTheFramesThatWillPlay) {
    // Before any frame plays, the next frame is the start frame.
    std::vector<int> channels;
    for (const auto& frame : Animation(3, true, false, 6).Upcoming(4)) {
        channels.push_back(frame.channel());
    }
    EXPECT_EQ(channels, (std::vector<int>{6, 9, 0, 3}));
    EXPECT_EQ(Animation(3, false, false, 6).Upcoming(4).size(), 2) << "an animation that stops has no frames after its end";
}

// The pixel fixture has two channels and three stokes. Each chunk is one channel and one stokes deep,
// so each plane is a run of its own.
std::shared_ptr<PlaneReadAhead> Load() {
    std::shared_ptr<FileLoader> loader(FileLoader::GetLoader(ZARR_PIXEL_FIXTURE));
    loader->OpenFile("");
    return std::shared_ptr<PlaneReadAhead>(loader, loader->ReadAhead());
}

// Reads the planes of another loader, but through a cache pool of its own.
class ThroughPool : public PlaneReadAhead {
public:
    ThroughPool(std::shared_ptr<PlaneReadAhead> planes, std::size_t bytes)
        : _planes(std::move(planes)), _pool(GetZarrContext()->CreateCachePool(bytes).value()) {}
    std::optional<ZarrPlaneRead> Plane(int z, int stokes) const override {
        auto plane = _planes->Plane(z, stokes);
        if (plane) {
            plane->options.control.cache_pool = _pool;
        }
        return plane;
    }

private:
    std::shared_ptr<PlaneReadAhead> _planes;
    carta::zarr::CachePool _pool;
};

void WaitUntilIdle(const AnimationReadAhead& read_ahead) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (read_ahead.Stats().under_way && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(read_ahead.Stats().under_way) << "a prefetch never finished";
}

TEST(AnimationReadAhead, AFileWhoseLoaderCannotReadAheadIsLeftOut) {
    const ScopedZarrCache cache(1.0 / 16);
    EXPECT_FALSE(AnimationReadAhead::For({{0, nullptr}}));
    const auto planes = Load();
    ASSERT_NE(planes, nullptr);
    ASSERT_TRUE(planes->Plane(0, 0).has_value());
    EXPECT_TRUE(AnimationReadAhead::For({{0, nullptr}, {1, planes}})) << "the file that can read ahead was left out too";
}

TEST(AnimationReadAhead, TheCacheAskedIsTheOneThePlanesAreReadThrough) {
    EXPECT_FALSE(AnimationReadAhead::For({{0, std::make_shared<ThroughPool>(Load(), 1)}})) << "a cache of one byte was accepted";
    EXPECT_TRUE(AnimationReadAhead::For({{0, std::make_shared<ThroughPool>(Load(), std::size_t(64) << 20)}}));
}

TEST(AnimationReadAhead, EachFileHasTheNextRunOfItsOwnPlanesDecoded) {
    // The planes are read through the shared cache.
    const ScopedZarrCache cache(1.0 / 16);
    const auto active = Load();
    const auto matched = Load();
    auto read_ahead = AnimationReadAhead::For({{3, active}, {8, matched}});
    ASSERT_TRUE(read_ahead);
    EXPECT_FALSE(active->Plane(0, 3).has_value()) << "a stokes that the file does not have has a plane";

    // File 5 is not animated, and file 8 has no stokes 3. Both are skipped.
    read_ahead->Served(
        std::chrono::steady_clock::now(), false, {{3, 0, 0}, {8, 0, 1}, {5, 0, 0}}, {{{3, 1, 0}, {8, 0, 3}}, {{3, 1, 0}, {8, 1, 1}}});
    WaitUntilIdle(*read_ahead);
    EXPECT_EQ(read_ahead->Stats().prefetches, 2) << "the next run of each animated file was not decoded once";
}

} // namespace
