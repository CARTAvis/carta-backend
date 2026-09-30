/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "ImageGenerators/SlabPlan.h"

using namespace carta;
using casacore::IPosition;

namespace {

constexpr std::uint64_t kGiB = std::uint64_t(1) << 30;
constexpr std::uint64_t kMiB = std::uint64_t(1) << 20;

// Enough memory that the ceiling, not the machine, sets the budget: 2 GiB.
constexpr std::uint64_t kLargeMachine = 64 * kGiB;

constexpr unsigned kFloat = 4;
constexpr unsigned kMaskedFloat = 5;

} // namespace

// The cube the ceiling was measured on: 512 x 512 x 7776, chunked 512x512x4. The fastest display axis
// gets exactly one chunk, and the next what the budget has left. At 2 GiB the measurement read the
// store 3.7x over, which is what the plan predicts.
TEST(SlabPlanTest, ADeepCubeGetsOneChunkAcrossAndWhatTheBudgetLeavesDown) {
    const auto plan = PlanSlab(IPosition(4, 512, 512, 7776, 1), IPosition(4, 512, 512, 4, 1), 2, kFloat, kLargeMachine);
    EXPECT_TRUE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(4, 512, 134, 7776, 1));
    // The axis that ran out of budget is stepped first, then the one that got its whole chunk.
    EXPECT_EQ(plan.axis_path, IPosition(4, 3, 1, 0, 2));
    EXPECT_EQ(plan.budget_bytes, 2 * kGiB);
    EXPECT_EQ(plan.unit_bytes, std::uint64_t(512) * 512 * 7776 * kFloat);
    EXPECT_NEAR(plan.StoreReads(), 3.8, 0.05);
}

// A mask costs a byte a pixel more to hold, and the slab shrinks by that much.
TEST(SlabPlanTest, AMaskTakesItsShareOfTheBudget) {
    const auto plan = PlanSlab(IPosition(4, 512, 512, 7776, 1), IPosition(4, 512, 512, 4, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(plan.slab, IPosition(4, 512, 107, 7776, 1));
}

TEST(SlabPlanTest, AWideCubeStaysOneChunkAcross) {
    const auto plan = PlanSlab(IPosition(4, 4096, 4096, 2000, 1), IPosition(4, 512, 512, 4, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(plan.slab, IPosition(4, 512, 419, 2000, 1));
    EXPECT_EQ(plan.axis_path, IPosition(4, 3, 1, 0, 2));
}

// Current behaviour, not the right answer. A Zarr image's cursor advice is not its chunk: it grows the
// chunk along the fastest axis to a million pixels, so a 512x512x1 chunk is advised as 2048 x 512 --
// and the plan, which can only go by the advice, spans four chunks across where it meant one. By its
// own reckoning that reads the store 4.9x over, where the chunk itself would have been 1.2x. What is
// taken for the chunk is the caller's to change; this pins what it is today.
TEST(SlabPlanTest, AnAdviceGrownPastTheChunkIsTakenForTheChunk) {
    const IPosition shape(4, 4096, 4096, 2000, 1);
    const auto advised = PlanSlab(shape, IPosition(4, 2048, 512, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(advised.slab, IPosition(4, 2048, 104, 2000, 1));
    EXPECT_NEAR(advised.StoreReads(), 4.9, 0.05);

    const auto chunk = PlanSlab(shape, IPosition(4, 512, 512, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(chunk.slab, IPosition(4, 512, 419, 2000, 1));
    EXPECT_NEAR(chunk.StoreReads(), 1.2, 0.05);
}

// A FITS image advises tiles of whole rows, and gets a slab of them.
TEST(SlabPlanTest, WholeRowTilesGiveASlabOfWholeRows) {
    const auto plan = PlanSlab(IPosition(4, 2048, 2048, 2000, 1), IPosition(4, 2048, 16, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_TRUE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(4, 2048, 16, 2000, 1));
}

// When a whole unit fits, the budget is the unit and the store is read once.
TEST(SlabPlanTest, AUnitThatFitsIsReadOnce) {
    const auto plan = PlanSlab(IPosition(4, 1024, 1024, 100, 1), IPosition(4, 256, 256, 10, 1), 2, kFloat, kLargeMachine);
    EXPECT_EQ(plan.slab, IPosition(4, 256, 256, 100, 1));
    EXPECT_EQ(plan.budget_bytes, plan.unit_bytes);
    EXPECT_DOUBLE_EQ(plan.StoreReads(), 1.0);
}

// An advice larger than the image -- a region smaller than a chunk -- is cut to the image.
TEST(SlabPlanTest, AnAdviceLargerThanTheImageIsCutToIt) {
    const auto plan = PlanSlab(IPosition(4, 300, 300, 100, 1), IPosition(4, 512, 512, 4, 1), 2, kFloat, kLargeMachine);
    EXPECT_TRUE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(4, 300, 300, 100, 1));
}

TEST(SlabPlanTest, OtherAxisCountsAndOrders) {
    const auto three = PlanSlab(IPosition(3, 1024, 1024, 500), IPosition(3, 256, 256, 8), 2, kFloat, kLargeMachine);
    EXPECT_EQ(three.slab, IPosition(3, 256, 256, 500));
    EXPECT_EQ(three.axis_path, IPosition(3, 1, 0, 2));

    const auto first = PlanSlab(IPosition(3, 1000, 800, 50), IPosition(3, 100, 100, 10), 0, kFloat, kLargeMachine);
    EXPECT_EQ(first.slab, IPosition(3, 1000, 100, 10));
    EXPECT_EQ(first.axis_path, IPosition(3, 2, 1, 0));
}

// A line longer than the budget still gets a slab: one line.
TEST(SlabPlanTest, ALineLongerThanTheBudgetIsReadALineAtATime) {
    const auto plan = PlanSlab(IPosition(3, 4, 4, 600000000), IPosition(3, 2, 2, 1000), 2, kFloat, kLargeMachine);
    EXPECT_TRUE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(3, 1, 1, 600000000));
    EXPECT_EQ(plan.axis_path, IPosition(3, 1, 0, 2));
}

// The budget is a sixteenth of the machine up to the ceiling, and never below 64 MiB -- which is also
// what a machine that will not say how much memory it has is given.
TEST(SlabPlanTest, TheBudgetFollowsTheMachineBetweenAFloorAndACeiling) {
    const IPosition shape(4, 512, 512, 7776, 1);
    const IPosition chunk(4, 512, 512, 4, 1);

    const auto sixteen = PlanSlab(shape, chunk, 2, kFloat, 16 * kGiB);
    EXPECT_EQ(sixteen.budget_bytes, 1 * kGiB);
    EXPECT_EQ(sixteen.slab, IPosition(4, 512, 67, 7776, 1));

    const auto small = PlanSlab(shape, chunk, 2, kFloat, 512 * kMiB);
    EXPECT_EQ(small.budget_bytes, 64 * kMiB);
    EXPECT_EQ(small.slab, IPosition(4, 512, 4, 7776, 1));

    const auto unknown = PlanSlab(shape, chunk, 2, kFloat, 0);
    EXPECT_EQ(unknown.budget_bytes, 64 * kMiB);

    const auto huge = PlanSlab(shape, chunk, 2, kFloat, 1024 * kGiB);
    EXPECT_EQ(huge.budget_bytes, 2 * kGiB);
}

// An image that decodes as a whole, or cannot say how it decodes, is read in whole lines by a fixed
// 20 MB budget, filling the display axes in their natural order.
TEST(SlabPlanTest, WithNothingToAlignToTheSlabIsAByteBudget) {
    const auto whole = PlanSlab(IPosition(4, 4096, 4096, 2000, 1), IPosition(4, 4096, 4096, 2000, 1), 2, kFloat, kLargeMachine);
    EXPECT_FALSE(whole.chunked);
    EXPECT_TRUE(std::isnan(whole.StoreReads()));
    EXPECT_EQ(whole.slab, IPosition(4, 2500, 1, 2000, 1));
    EXPECT_EQ(whole.axis_path, IPosition(4, 0, 1, 2, 3));

    const auto small = PlanSlab(IPosition(4, 64, 64, 10, 1), IPosition(4, 64, 64, 10, 1), 2, kFloat, kLargeMachine);
    EXPECT_EQ(small.slab, IPosition(4, 64, 64, 10, 1));

    const auto masked = PlanSlab(IPosition(4, 512, 512, 7776, 1), IPosition(4, 512, 512, 7776, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_FALSE(masked.chunked);
    EXPECT_EQ(masked.slab, IPosition(4, 512, 1, 7776, 1));

    const auto zero = PlanSlab(IPosition(4, 512, 512, 100, 1), IPosition(4, 0, 512, 4, 1), 2, kFloat, kLargeMachine);
    EXPECT_FALSE(zero.chunked);
    EXPECT_EQ(zero.slab, IPosition(4, 512, 97, 100, 1));

    const auto short_advice = PlanSlab(IPosition(4, 512, 512, 100, 1), IPosition(3, 512, 512, 4), 2, kFloat, kLargeMachine);
    EXPECT_FALSE(short_advice.chunked);
    EXPECT_EQ(short_advice.slab, IPosition(4, 512, 97, 100, 1));
}

// A line too long for the byte budget is read one at a time.
TEST(SlabPlanTest, ALineLongerThanTheByteBudgetIsReadALineAtATime) {
    const auto plan = PlanSlab(IPosition(3, 4, 4, 6000000), IPosition(3, 4, 4, 6000000), 2, kFloat, kLargeMachine);
    EXPECT_FALSE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(3, 1, 1, 6000000));
}
