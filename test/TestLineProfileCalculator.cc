/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// The line profiles, made from routes that do as a test says. Where the boxes are and how the pixels
// are read belong to the routes, and are tested with a loader in TestZarrImage.

#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "Region/RegionAnalysis/LineProfileCalculator.h"

using namespace carta;

namespace {

constexpr std::size_t kBoxes = 3;
constexpr std::size_t kChannels = 4;

// What every route says a box's mean is, so that a profile shows where it came from. Box 1 has no mean
// in channel 2.
float MeanOf(std::size_t box, std::size_t channel) {
    if (box == 1 && channel == 2) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>((box * 100) + channel);
}

// A route that hands the profiles over as a walk does -- runs of channels of every box, each first
// half read and then complete -- or as the boxes one at a time do, or declines, or fails after its
// first block.
class FakeRoute : public BoxReducer {
public:
    enum class Mode { walk, boxes, decline, fail };

    explicit FakeRoute(Mode mode) : _mode(mode) {}

    BatchOutcome BoxMeans(
        const std::function<bool()>& cancellation_requested, const std::function<bool(const LineProfileBlock&)>& sink) override {
        ++calls;
        handed_a_predicate = static_cast<bool>(cancellation_requested);
        if (_mode == Mode::decline) {
            return BatchOutcome::declined;
        }
        std::vector<LineProfileBlock> blocks;
        if (_mode == Mode::boxes) {
            for (std::size_t box = 0; box < kBoxes; ++box) {
                blocks.push_back(Block(box, 1, 0, kChannels, true, 1.0));
            }
        } else {
            for (std::size_t first = 0; first < kChannels; first += 2) {
                blocks.push_back(Block(0, kBoxes, first, 2, false, 0.5));
                blocks.push_back(Block(0, kBoxes, first, 2, true, 1.0));
            }
        }
        for (const auto& block : blocks) {
            if (_mode == Mode::boxes && cancellation_requested && cancellation_requested()) {
                return BatchOutcome::cancelled;
            }
            ++blocks_handed;
            if (!sink(block)) {
                return BatchOutcome::cancelled;
            }
            if (_mode == Mode::fail) {
                return BatchOutcome::failed;
            }
        }
        return BatchOutcome::finished;
    }

    int calls = 0;
    int blocks_handed = 0;
    bool handed_a_predicate = false;

private:
    static LineProfileBlock Block(std::size_t first_box, std::size_t box_count, std::size_t first_channel, std::size_t channel_count,
        bool complete, double completeness) {
        LineProfileBlock block;
        block.first_box = first_box;
        block.box_count = box_count;
        block.first_channel = first_channel;
        block.channel_count = channel_count;
        block.complete = complete;
        block.completeness = completeness;
        // A block still filling says something that is not yet the answer.
        block.mean = [first_box, first_channel, complete](std::size_t box, std::size_t channel) {
            return complete ? MeanOf(first_box + box, first_channel + channel) : -1.0f;
        };
        return block;
    }

    Mode _mode;
};

// The routes to ask, in turn.
std::vector<LineRoute> Routes(std::initializer_list<FakeRoute*> routes) {
    std::vector<LineRoute> named;
    for (auto* route : routes) {
        named.push_back({"fake", route});
    }
    return named;
}

void ExpectEveryMean(const casacore::Matrix<float>& profiles, bool reverse) {
    ASSERT_EQ(profiles.shape()(0), static_cast<ssize_t>(reverse ? kChannels : kBoxes));
    ASSERT_EQ(profiles.shape()(1), static_cast<ssize_t>(reverse ? kBoxes : kChannels));
    for (std::size_t box = 0; box < kBoxes; ++box) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float actual = reverse ? profiles(channel, box) : profiles(box, channel);
            const float expected = MeanOf(box, channel);
            if (std::isnan(expected)) {
                EXPECT_TRUE(std::isnan(actual)) << "box " << box << " channel " << channel;
            } else {
                EXPECT_EQ(actual, expected) << "box " << box << " channel " << channel;
            }
        }
    }
}

} // namespace

TEST(LineProfileCalculatorTest, AWalkMakesEveryBoxsProfile) {
    FakeRoute walk(FakeRoute::Mode::walk);
    FakeRoute boxes(FakeRoute::Mode::boxes);
    casacore::Matrix<float> profiles;
    EXPECT_EQ(
        LineProfileCalculator(Routes({&walk, &boxes})).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::finished);
    ExpectEveryMean(profiles, false);
    EXPECT_EQ(boxes.calls, 0) << "a route that finished is the only one asked";
}

TEST(LineProfileCalculatorTest, TheBoxesOneAtATimeAnswerWhenTheWalkDeclines) {
    FakeRoute walk(FakeRoute::Mode::decline);
    FakeRoute boxes(FakeRoute::Mode::boxes);
    casacore::Matrix<float> profiles;
    EXPECT_EQ(
        LineProfileCalculator(Routes({&walk, &boxes})).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::finished);
    ExpectEveryMean(profiles, false);
    EXPECT_EQ(walk.calls, 1);
    EXPECT_EQ(boxes.calls, 1);
}

TEST(LineProfileCalculatorTest, EveryRouteDecliningIsAFailure) {
    FakeRoute walk(FakeRoute::Mode::decline);
    FakeRoute boxes(FakeRoute::Mode::decline);
    casacore::Matrix<float> profiles;
    EXPECT_EQ(LineProfileCalculator(Routes({&walk, &boxes})).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::failed);
    EXPECT_TRUE(profiles.empty());
}

// The boxes one at a time read the same pixels, and would meet the same failure after a part of an
// answer had been reported.
TEST(LineProfileCalculatorTest, AWalkThatFailsOnceItHasBegunIsNotFollowed) {
    FakeRoute walk(FakeRoute::Mode::fail);
    FakeRoute boxes(FakeRoute::Mode::boxes);
    casacore::Matrix<float> profiles;
    EXPECT_EQ(LineProfileCalculator(Routes({&walk, &boxes})).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::failed);
    EXPECT_EQ(boxes.calls, 0);
    EXPECT_TRUE(profiles.empty()) << "only a finished calculation writes the profiles";
}

TEST(LineProfileCalculatorTest, NoBoxesOrNoChannelsIsNothingToMake) {
    FakeRoute boxes(FakeRoute::Mode::boxes);
    casacore::Matrix<float> profiles;
    EXPECT_EQ(LineProfileCalculator(Routes({&boxes})).Calculate(0, kChannels, false, {}, profiles), LineProfileOutcome::failed);
    EXPECT_EQ(LineProfileCalculator(Routes({&boxes})).Calculate(kBoxes, 0, false, {}, profiles), LineProfileOutcome::failed);
    EXPECT_EQ(boxes.calls, 0);
}

TEST(LineProfileCalculatorTest, ReversedProfilesAreAChannelToARow) {
    for (const auto mode : {FakeRoute::Mode::walk, FakeRoute::Mode::boxes}) {
        FakeRoute route(mode);
        casacore::Matrix<float> profiles;
        ASSERT_EQ(LineProfileCalculator(Routes({&route})).Calculate(kBoxes, kChannels, true, {}, profiles), LineProfileOutcome::finished);
        ExpectEveryMean(profiles, true);
    }
}

// A box a route says nothing about has no profile: a box that did not land on the image, which a walk
// leaves out.
TEST(LineProfileCalculatorTest, ABoxNoRouteSpokeForIsNaN) {
    class TwoOfThree : public BoxReducer {
    public:
        BatchOutcome BoxMeans(const std::function<bool()>&, const std::function<bool(const LineProfileBlock&)>& sink) override {
            for (const std::size_t box : {0u, 2u}) {
                LineProfileBlock block;
                block.first_box = box;
                block.box_count = 1;
                block.channel_count = kChannels;
                block.mean = [box](std::size_t, std::size_t channel) { return MeanOf(box, channel); };
                sink(block);
            }
            return BatchOutcome::finished;
        }
    } route;
    casacore::Matrix<float> profiles;
    ASSERT_EQ(
        LineProfileCalculator({{"two of three", &route}}).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::finished);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_TRUE(std::isnan(profiles(1, channel))) << channel;
        EXPECT_EQ(profiles(2, channel), MeanOf(2, channel));
    }
}

TEST(LineProfileCalculatorTest, ABlockOutsideTheProfilesIsAFailure) {
    class Overreaching : public BoxReducer {
    public:
        BatchOutcome BoxMeans(const std::function<bool()>&, const std::function<bool(const LineProfileBlock&)>& sink) override {
            LineProfileBlock block;
            block.first_box = kBoxes - 1;
            block.box_count = 2;
            block.channel_count = kChannels;
            block.mean = [](std::size_t, std::size_t) { return 0.0f; };
            return sink(block) ? BatchOutcome::finished : BatchOutcome::cancelled;
        }
    } route;
    casacore::Matrix<float> profiles;
    EXPECT_EQ(
        LineProfileCalculator({{"overreaching", &route}}).Calculate(kBoxes, kChannels, false, {}, profiles), LineProfileOutcome::failed);
    EXPECT_TRUE(profiles.empty());
}

// The share of every box's every channel that is final, a block still filling counted for the part of
// it read: the same fraction of a walk's runs of channels as of the boxes one at a time.
TEST(LineProfileCalculatorTest, ProgressIsTheShareOfTheProfilesMade) {
    const std::vector<float> walked{0.25f, 0.5f, 0.75f, 1.0f};
    const std::vector<float> boxed{1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    for (const auto& [mode, expected] : {std::pair<FakeRoute::Mode, std::vector<float>>{FakeRoute::Mode::walk, walked},
             std::pair<FakeRoute::Mode, std::vector<float>>{FakeRoute::Mode::boxes, boxed}}) {
        FakeRoute route(mode);
        std::vector<float> reports;
        LineProfileControl control;
        control.progress = [&](float progress) { reports.push_back(progress); };
        casacore::Matrix<float> profiles;
        ASSERT_EQ(
            LineProfileCalculator(Routes({&route})).Calculate(kBoxes, kChannels, false, control, profiles), LineProfileOutcome::finished);
        ASSERT_EQ(reports.size(), expected.size());
        for (std::size_t i = 0; i < reports.size(); ++i) {
            EXPECT_FLOAT_EQ(reports[i], expected[i]) << "report " << i;
        }
    }
}

// A route that says nothing on the way still ends at one.
TEST(LineProfileCalculatorTest, AFinishedCalculationEndsAtOne) {
    class Silent : public BoxReducer {
    public:
        BatchOutcome BoxMeans(const std::function<bool()>&, const std::function<bool(const LineProfileBlock&)>&) override {
            return BatchOutcome::finished;
        }
    } route;
    std::vector<float> reports;
    LineProfileControl control;
    control.progress = [&](float progress) { reports.push_back(progress); };
    casacore::Matrix<float> profiles;
    ASSERT_EQ(
        LineProfileCalculator({{"silent", &route}}).Calculate(kBoxes, kChannels, false, control, profiles), LineProfileOutcome::finished);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports.back(), 1.0f);
    EXPECT_TRUE(std::isnan(profiles(0, 0)));
}

TEST(LineProfileCalculatorTest, AStopEndsTheCalculationBeforeTheNextBlockIsTaken) {
    for (const auto mode : {FakeRoute::Mode::walk, FakeRoute::Mode::boxes}) {
        FakeRoute route(mode);
        int asked = 0;
        LineProfileControl control;
        // Stop once the first block is in.
        control.cancellation_requested = [&]() { return ++asked > 1; };
        casacore::Matrix<float> profiles;
        EXPECT_EQ(
            LineProfileCalculator(Routes({&route})).Calculate(kBoxes, kChannels, false, control, profiles), LineProfileOutcome::cancelled);
        EXPECT_TRUE(route.handed_a_predicate) << "a route stops between blocks of its own too";
        EXPECT_EQ(route.blocks_handed, mode == FakeRoute::Mode::walk ? 2 : 1);
        EXPECT_TRUE(profiles.empty()) << "a stopped calculation writes nothing";
    }
}
