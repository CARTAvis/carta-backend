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
// gets exactly one chunk, and the next what the budget has left: 134 rows, so four slabs touch the
// chunk down.
TEST(SlabPlanTest, ADeepCubeGetsOneChunkAcrossAndWhatTheBudgetLeavesDown) {
    const auto plan = PlanSlab(IPosition(4, 512, 512, 7776, 1), IPosition(4, 512, 512, 4, 1), 2, kFloat, kLargeMachine);
    EXPECT_TRUE(plan.chunked);
    EXPECT_EQ(plan.slab, IPosition(4, 512, 134, 7776, 1));
    // The axis that ran out of budget is stepped first, then the one that got its whole chunk.
    EXPECT_EQ(plan.axis_path, IPosition(4, 3, 1, 0, 2));
    EXPECT_EQ(plan.budget_bytes, 2 * kGiB);
    EXPECT_EQ(plan.unit_bytes, std::uint64_t(512) * 512 * 7776 * kFloat);
    EXPECT_DOUBLE_EQ(plan.store_reads, 4.0);
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
    // 419 rows from the corner touch each 512-row chunk twice, and one of the eight three times.
    EXPECT_DOUBLE_EQ(plan.store_reads, 2.125);
}

// What happens where only a cursor advice is known. A Zarr image's is not its chunk: it grows the chunk
// along the fastest axis to a million pixels, so a 512x512x1 chunk is advised as 2048 x 512, and a plan
// from the advice spans four chunks across where it meant one. Measured on 2048 x 2048 x 2000, the
// advice's plan decoded the store 5.4 times over and the chunk's 1.7 -- which is why a Zarr image's
// moment is given its chunks instead (see ImageMoments::SetChunkGrid).
TEST(SlabPlanTest, AnAdviceGrownPastTheChunkIsTakenForTheChunk) {
    const IPosition shape(4, 4096, 4096, 2000, 1);
    const auto advised = PlanSlab(shape, IPosition(4, 2048, 512, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(advised.slab, IPosition(4, 2048, 104, 2000, 1));
    // 104 rows stepped from the corner drift across the 512-row chunks: 5 or 6 slabs touch each.
    EXPECT_DOUBLE_EQ(advised.store_reads, 5.875);

    const auto chunk = PlanSlab(shape, IPosition(4, 512, 512, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(chunk.slab, IPosition(4, 512, 419, 2000, 1));
    EXPECT_DOUBLE_EQ(chunk.store_reads, 2.125);
}

// The count follows where the chunks lie. A region whose corner is off the grid has its slabs, stepped
// from that corner, straddle the chunks across as well as down; told nothing of the grid, the plan takes
// it to start at the region's corner and cannot see that. Measured on a 1536-square region at 256,256
// of the cube above, the store was decoded 2.9 times over.
TEST(SlabPlanTest, TheCountFollowsWhereTheGridLies) {
    const IPosition shape(4, 1536, 1536, 2000, 1);
    const IPosition chunk(4, 512, 512, 1, 1);

    const auto blind = PlanSlab(shape, chunk, 2, kMaskedFloat, kLargeMachine);
    EXPECT_DOUBLE_EQ(blind.store_reads, 2.0);

    const auto told = PlanSlab(shape, chunk, 2, kMaskedFloat, kLargeMachine, IPosition(4, 256, 256, 0, 0));
    EXPECT_DOUBLE_EQ(told.store_reads, 2.625);
    // Only the count: the slabs are what they were.
    EXPECT_EQ(told.slab, blind.slab);
    EXPECT_EQ(told.axis_path, blind.axis_path);
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
    EXPECT_DOUBLE_EQ(plan.store_reads, 1.0);
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
    EXPECT_DOUBLE_EQ(plan.store_reads, 4.0);
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
    EXPECT_TRUE(std::isnan(whole.store_reads));
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

// What a cache of decoded chunks has to hold for every chunk to be decoded once: every chunk one slab
// touches, since which of them the next slab needs again depends on the order they were decoded in. On
// the cube the 4 GiB measurement was made on, a slab one chunk across and two down, the whole depth of
// 512x512x1 chunks: 4000 chunks, a little under 4 GiB.
TEST(SlabPlanTest, ACacheHoldsEveryChunkOneSlabTouches) {
    const auto plan = PlanSlab(IPosition(4, 2048, 2048, 2000, 1), IPosition(4, 512, 512, 1, 1), 2, kMaskedFloat, kLargeMachine);
    EXPECT_EQ(plan.slab, IPosition(4, 512, 419, 2000, 1));
    EXPECT_EQ(plan.reuse_pixels, std::uint64_t(1 * 2 * 2000) * 512 * 512);
    EXPECT_EQ(plan.CacheBytes(kFloat), std::uint64_t(4000) * 512 * 512 * kFloat);
    // Counted in what a chunk decodes to rather than in floats: the same 4000 chunks, at whatever one of
    // them holds -- here one byte a pixel, to stay under the ceiling -- and held at the ceiling past it,
    // as a float64 chunk with its flag beside it, nine bytes a pixel, is.
    EXPECT_EQ(plan.reuse_chunks, 4000U);
    EXPECT_EQ(plan.CacheBytesOfChunks(std::uint64_t(512) * 512), std::uint64_t(4000) * 512 * 512);
    EXPECT_EQ(plan.CacheBytesOfChunks(std::uint64_t(512) * 512 * 9), plan.cache_ceiling_bytes);
}

// Off the grid a slab straddles the chunks across as well, and needs twice as many kept -- more than a
// sixteenth of a 64 GiB machine, which is what the cache gets at most. The slab's own ceiling, which is
// about the collapse, does not apply.
TEST(SlabPlanTest, ACacheIsASixteenthOfTheMachineAtMost) {
    const IPosition shape(4, 1536, 1536, 2000, 1);
    const IPosition chunk(4, 512, 512, 1, 1);
    const IPosition corner(4, 256, 256, 0, 0);

    const auto plan = PlanSlab(shape, chunk, 2, kMaskedFloat, kLargeMachine, corner);
    EXPECT_EQ(plan.reuse_pixels, std::uint64_t(2 * 2 * 2000) * 512 * 512);
    EXPECT_EQ(plan.CacheBytes(kFloat), 4 * kGiB);

    const auto roomy = PlanSlab(shape, chunk, 2, kMaskedFloat, 1024 * kGiB, corner);
    EXPECT_EQ(roomy.CacheBytes(kFloat), std::uint64_t(8000) * 512 * 512 * kFloat);

    // A machine that will not say how much memory it has gets the slab's floor.
    const auto unknown = PlanSlab(shape, chunk, 2, kMaskedFloat, 0, corner);
    EXPECT_EQ(unknown.CacheBytes(kFloat), 64 * kMiB);
}

// Nothing to keep where no chunk is decoded twice: a slab that holds whole units touches each once, and a
// slab that was not shaped to chunks says nothing about them.
TEST(SlabPlanTest, NoCacheWhereNothingIsDecodedTwice) {
    const auto whole_units = PlanSlab(IPosition(4, 1024, 1024, 100, 1), IPosition(4, 256, 256, 10, 1), 2, kFloat, kLargeMachine);
    EXPECT_DOUBLE_EQ(whole_units.store_reads, 1.0);
    EXPECT_EQ(whole_units.reuse_pixels, 0U);
    EXPECT_EQ(whole_units.CacheBytes(kFloat), 0U);
    EXPECT_EQ(whole_units.CacheBytesOfChunks(256 * 256 * 10 * kFloat), 0U);

    const auto by_bytes = PlanSlab(IPosition(4, 4096, 4096, 2000, 1), IPosition(4, 4096, 4096, 2000, 1), 2, kFloat, kLargeMachine);
    EXPECT_EQ(by_bytes.reuse_pixels, 0U);
    EXPECT_EQ(by_bytes.CacheBytes(kFloat), 0U);
}

// What the cache saves is the slab stepped next sharing what the last one decoded, so it saves along
// the axis stepped first. Across the others the slab that shared a chunk came a whole sweep earlier and
// is long gone: a region off the grid, whose slabs straddle the chunks across, still decodes the
// columns they share twice. Measured on that region, chunk-weighted 1.5 and by bytes 1.635, the store
// was decoded 1.62 times over with the cache and 2.88 without.
TEST(SlabPlanTest, ACacheSavesAlongTheAxisSteppedFirst) {
    const IPosition chunk(4, 512, 512, 1, 1);

    const auto whole = PlanSlab(IPosition(4, 2048, 2048, 2000, 1), chunk, 2, kMaskedFloat, kLargeMachine);
    EXPECT_DOUBLE_EQ(whole.store_reads, 2.0);
    EXPECT_DOUBLE_EQ(whole.cached_store_reads, 1.0);

    const auto off_the_grid = PlanSlab(IPosition(4, 1536, 1536, 2000, 1), chunk, 2, kMaskedFloat, kLargeMachine, IPosition(4, 256, 256, 0, 0));
    EXPECT_EQ(off_the_grid.axis_path, IPosition(4, 3, 1, 0, 2));
    EXPECT_DOUBLE_EQ(off_the_grid.store_reads, 2.625);
    EXPECT_DOUBLE_EQ(off_the_grid.cached_store_reads, 1.5);

    // Nothing decoded twice, nothing to save.
    const auto whole_units = PlanSlab(IPosition(4, 1024, 1024, 100, 1), IPosition(4, 256, 256, 10, 1), 2, kFloat, kLargeMachine);
    EXPECT_DOUBLE_EQ(whole_units.cached_store_reads, 1.0);

    const auto by_bytes = PlanSlab(IPosition(4, 4096, 4096, 2000, 1), IPosition(4, 4096, 4096, 2000, 1), 2, kFloat, kLargeMachine);
    EXPECT_TRUE(std::isnan(by_bytes.cached_store_reads));
}
