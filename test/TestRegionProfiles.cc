/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// Region profiles made a step at a time from a reader that does as a test says. How a loader reads is
// the loader's, and is tested with its files in TestHdf5Image, TestReportedStatistics and TestZarrImage.

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "Region/RegionAnalysis/RegionProfiles.h"

using namespace carta;
using namespace std::chrono_literals;

namespace {

using ProfilesMap = std::map<CARTA::StatsType, std::vector<double>>;

constexpr std::uint64_t kPieces = 4;

// A reader that proceeds in four pieces, each adding one pixel of value 2 to every channel, one piece
// per call however long the step; or declines; or fails after its first piece; or reports halfway
// through each piece.
class FakeReader : public RegionProfileReader {
public:
    enum class Mode { serve, decline, fail, report };

    explicit FakeReader(Mode mode = Mode::serve, double beam_area = std::nan("")) : mode(mode), beam_area(beam_area) {}

    BatchOutcome ReadOn(const RegionProfileRequest& request, std::mutex&, RegionProfileProgress& progress,
        std::chrono::steady_clock::time_point, const std::function<bool(double)>& report) override {
        ++calls;
        if (mode == Mode::decline) {
            return BatchOutcome::declined;
        }
        if (progress.total == 0) {
            progress.total = kPieces;
        }
        last_request = request;
        if (mode == Mode::report) {
            const double halfway = (static_cast<double>(progress.done) + 0.5) / static_cast<double>(progress.total);
            if (!report(halfway)) {
                return BatchOutcome::cancelled;
            }
        }
        for (auto& channel : progress.channels) {
            channel.read = true;
            channel.num_pixels += 1;
            channel.sum += 2.0;
            channel.sum_sq += 4.0;
            channel.min = std::min(channel.min, 2.0);
            channel.max = std::max(channel.max, 2.0);
        }
        ++progress.done;
        if (mode == Mode::fail) {
            return BatchOutcome::failed;
        }
        return BatchOutcome::finished;
    }

    double BeamArea() override {
        return beam_area;
    }

    Mode mode;
    double beam_area;
    int calls = 0;
    RegionProfileRequest last_request;
};

// A request for a 2 x 3 box at (1, 1), over channels 0 to 2.
struct Asked {
    casacore::ArrayLattice<casacore::Bool> mask{casacore::Array<casacore::Bool>(casacore::IPosition(2, 2, 3), true)};
    RegionProfileRequest request;

    Asked() {
        request.origin = casacore::IPosition(2, 1, 1);
        request.mask = &mask;
        request.channels = AxisRange(0, 2);
    }
};

BatchOutcome Step(RegionProfiles& profiles, FakeReader& reader, const RegionProfileRequest& request, ProfilesMap& profile, float& progress,
    RegionProfileKey key = {0, 1, 0}, const RegionProfileReport& report = {}) {
    static std::mutex image_mutex;
    return profiles.Continue(key, request, reader, image_mutex, 0ms, report, profile, progress);
}

} // namespace

TEST(RegionProfilesTest, AProfileIsMadeAStepAtATimeAndResumed) {
    RegionProfiles profiles;
    FakeReader reader;
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    for (std::uint64_t step = 1; step <= kPieces; ++step) {
        ASSERT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::finished);
        EXPECT_FLOAT_EQ(progress, static_cast<float>(step) / kPieces);
        EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), static_cast<double>(step)) << "each step goes on from the last";
    }
    ASSERT_EQ(profile.at(CARTA::StatsType::Mean).size(), 3u);
    EXPECT_EQ(profile.at(CARTA::StatsType::Mean).at(2), 2.0);
}

TEST(RegionProfilesTest, AFinishedProfileIsAnsweredWithoutReadingAgain) {
    RegionProfiles profiles;
    FakeReader reader;
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    while (progress < 1.0f) {
        ASSERT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::finished);
    }
    const int calls = reader.calls;
    ProfilesMap again;
    float again_progress = 0.0f;
    ASSERT_EQ(Step(profiles, reader, asked.request, again, again_progress), BatchOutcome::finished);
    EXPECT_EQ(reader.calls, calls);
    EXPECT_EQ(again_progress, 1.0f);
    EXPECT_EQ(again.at(CARTA::StatsType::Sum), profile.at(CARTA::StatsType::Sum));
}

// A request is resumed only if it is the same question: the same box, mask extent and channels.
TEST(RegionProfilesTest, AnyOtherRequestStartsAgain) {
    const auto started_again = [](const std::function<void(RegionProfileRequest&)>& change) {
        RegionProfiles profiles;
        FakeReader reader;
        Asked asked;
        ProfilesMap profile;
        float progress = 0.0f;
        Step(profiles, reader, asked.request, profile, progress);
        Step(profiles, reader, asked.request, profile, progress);
        Asked other;
        change(other.request);
        Step(profiles, reader, other.request, profile, progress);
        return profile.at(CARTA::StatsType::NumPixels).at(0) == 1.0;
    };
    casacore::ArrayLattice<casacore::Bool> wider(casacore::Array<casacore::Bool>(casacore::IPosition(2, 3, 2), true));
    EXPECT_FALSE(started_again([](RegionProfileRequest&) {})) << "the same request goes on";
    EXPECT_TRUE(started_again([](RegionProfileRequest& request) { request.origin = casacore::IPosition(2, 5, 1); })) << "another box";
    EXPECT_TRUE(started_again([](RegionProfileRequest& request) { request.channels = AxisRange(1, 3); }))
        << "other channels of the same count";
    EXPECT_TRUE(started_again([&](RegionProfileRequest& request) { request.mask = &wider; })) << "another mask extent";
}

TEST(RegionProfilesTest, TheStokesAndTheFileAreProfilesOfTheirOwn) {
    RegionProfiles profiles;
    FakeReader reader;
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    Step(profiles, reader, asked.request, profile, progress, {0, 1, 0});
    Step(profiles, reader, asked.request, profile, progress, {0, 1, 1});
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 1.0);
    Step(profiles, reader, asked.request, profile, progress, {2, 1, 0});
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 1.0);
    EXPECT_EQ(profiles.Size(), 3u);
}

TEST(RegionProfilesTest, ReleasingARegionLetsItsProfilesGo) {
    RegionProfiles profiles;
    FakeReader reader;
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    for (const RegionProfileKey& key : {RegionProfileKey{0, 1, 0}, {0, 1, 1}, {1, 1, 0}, {0, 2, 0}, {1, 3, 0}}) {
        Step(profiles, reader, asked.request, profile, progress, key);
    }
    ASSERT_EQ(profiles.Size(), 5u);

    profiles.Release(1);
    EXPECT_EQ(profiles.Size(), 2u) << "every stokes and every file of region 1, and only those";
    Step(profiles, reader, asked.request, profile, progress, {0, 1, 0});
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 1.0) << "a released region starts again";

    profiles.ReleaseFile(1);
    EXPECT_EQ(profiles.Size(), 2u) << "file 1's region 3 has gone";
    profiles.Release(ALL_REGIONS);
    EXPECT_EQ(profiles.Size(), 0u);
}

TEST(RegionProfilesTest, ADeclineKeepsNothingAndLeavesTheProfileAlone) {
    RegionProfiles profiles;
    FakeReader reader(FakeReader::Mode::decline);
    Asked asked;
    ProfilesMap profile{{CARTA::StatsType::Sum, {7.0}}};
    float progress = 0.25f;
    EXPECT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::declined);
    EXPECT_EQ(profiles.Size(), 0u);
    EXPECT_EQ(profile.at(CARTA::StatsType::Sum), std::vector<double>{7.0});
    EXPECT_EQ(progress, 0.25f);
}

// A failure is the outcome of that step; what was read before it is kept, and a later step goes on.
TEST(RegionProfilesTest, AFailedStepKeepsWhatWasRead) {
    RegionProfiles profiles;
    FakeReader reader(FakeReader::Mode::fail);
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    EXPECT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::failed);
    EXPECT_TRUE(profile.empty()) << "a step that failed says nothing";
    reader.mode = FakeReader::Mode::serve;
    ASSERT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::finished);
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 2.0);
}

TEST(RegionProfilesTest, AReportCarriesTheProfileSoFarAndCanStopTheStep) {
    RegionProfiles profiles;
    FakeReader reader(FakeReader::Mode::report);
    Asked asked;
    ProfilesMap profile;
    float progress = 0.0f;
    ASSERT_EQ(Step(profiles, reader, asked.request, profile, progress), BatchOutcome::finished);

    std::vector<float> reported;
    double partial_pixels = -1.0;
    const RegionProfileReport keep = [&](float fraction, const std::function<ProfilesMap()>& partial) {
        reported.push_back(fraction);
        partial_pixels = partial().at(CARTA::StatsType::NumPixels).at(0);
        return true;
    };
    ASSERT_EQ(Step(profiles, reader, asked.request, profile, progress, {0, 1, 0}, keep), BatchOutcome::finished);
    ASSERT_EQ(reported.size(), 1u);
    EXPECT_FLOAT_EQ(reported[0], 1.5f / kPieces);
    EXPECT_EQ(partial_pixels, 1.0) << "the partial is the profile as it stood when the report was made";

    const RegionProfileReport stop = [](float, const std::function<ProfilesMap()>&) { return false; };
    EXPECT_EQ(Step(profiles, reader, asked.request, profile, progress, {0, 1, 0}, stop), BatchOutcome::cancelled);
}

TEST(RegionProfilesTest, AReaderThatSaysNothingOfHowFarHasFailed) {
    class Silent : public RegionProfileReader {
    public:
        BatchOutcome ReadOn(const RegionProfileRequest&, std::mutex&, RegionProfileProgress&, std::chrono::steady_clock::time_point,
            const std::function<bool(double)>&) override {
            return BatchOutcome::finished;
        }
        double BeamArea() override {
            return std::nan("");
        }
    } reader;
    RegionProfiles profiles;
    Asked asked;
    std::mutex image_mutex;
    ProfilesMap profile;
    float progress = 0.0f;
    EXPECT_EQ(profiles.Continue({0, 1, 0}, asked.request, reader, image_mutex, 0ms, {}, profile, progress), BatchOutcome::failed);
}

// What CARTA reports of a profile, from its totals.

TEST(RegionProfilesTest, AChannelNotYetReadIsUndefinedThroughout) {
    std::vector<ChannelTotals> channels(2);
    channels[0].read = true;
    channels[0].num_pixels = 2;
    channels[0].sum = 3.0;
    channels[0].sum_sq = 5.0;
    channels[0].min = 1.0;
    channels[0].max = 2.0;
    const auto stats = RegionProfileStatistics(channels, std::nan(""));
    for (const auto& [stat, values] : stats) {
        EXPECT_FALSE(std::isnan(values[0])) << static_cast<int>(stat);
        EXPECT_TRUE(std::isnan(values[1])) << static_cast<int>(stat);
    }
}

TEST(RegionProfilesTest, AChannelWithNoValidPixelHasOnlyItsCounts) {
    std::vector<ChannelTotals> channels(1);
    channels[0].read = true;
    channels[0].nan_count = 6;
    const auto stats = RegionProfileStatistics(channels, 2.0);
    for (const auto& [stat, values] : stats) {
        if (stat == CARTA::StatsType::NumPixels) {
            EXPECT_EQ(values[0], 0.0);
        } else if (stat == CARTA::StatsType::NanCount) {
            EXPECT_EQ(values[0], 6.0);
        } else {
            EXPECT_TRUE(std::isnan(values[0])) << static_cast<int>(stat);
        }
    }
}

TEST(RegionProfilesTest, ALonePixelHasSigmaZeroAndFluxIsTheSumOverTheBeam) {
    std::vector<ChannelTotals> channels(1);
    channels[0].read = true;
    channels[0].num_pixels = 1;
    channels[0].sum = -3.0;
    channels[0].sum_sq = 9.0;
    channels[0].min = -3.0;
    channels[0].max = -3.0;
    const auto with_beam = RegionProfileStatistics(channels, 1.5);
    EXPECT_EQ(with_beam.at(CARTA::StatsType::Sigma)[0], 0.0);
    EXPECT_EQ(with_beam.at(CARTA::StatsType::Extrema)[0], -3.0);
    EXPECT_EQ(with_beam.at(CARTA::StatsType::FluxDensity)[0], -2.0);
    EXPECT_EQ(RegionProfileStatistics(channels, std::nan("")).count(CARTA::StatsType::FluxDensity), 0u) << "no beam, no flux";
}
