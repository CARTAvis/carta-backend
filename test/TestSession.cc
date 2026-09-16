/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CommonTestUtilities.h"
#include "FileList/FileListHandler.h"
#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrContext.h"
#include "ImageData/ZarrLoader.h"
#include "Session/Session.h"
#include "Util/Message.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};
constexpr int kWidth = 4;
constexpr int kHeight = 5;
constexpr int kDepth = 2;

// What Frame::AutoBinSize works out to for this fixture: max(sqrt(4 * 5), 2) truncated.
constexpr int kAutoBins = 4;

// A Session with no socket and no event loop.
//
// That is the whole trick. Session::SendEvent already declines to hand anything to uWebSockets
// unless both are present, so with neither, every message the session would have sent stays in
// _out_msgs -- in order, encoded exactly as the frontend would have received it. No mock socket,
// no loop to pump, and nothing in the production path that exists only for tests.
class HeadlessSession : public Session {
public:
    HeadlessSession() : Session(nullptr, nullptr, 0, "", std::make_shared<FileListHandler>("/", ".")) {}

    using Session::CalculateCubeHistogram;
    using Session::SendRegionHistogramData;
    using Session::_frames;
    using Session::_histogram_context;
    using Session::_histogram_progress;
    using Session::_histogram_progress_interval;

    // Puts a frame the caller built where the session's own open would have put it. The tests that
    // steer the loader need a handle on it, and the open path does not hand one back.
    void AdoptFrame(int file_id, std::shared_ptr<Frame> frame) {
        _frames[file_id] = std::move(frame);
    }

    // Everything queued since the last call, oldest first, header and whole encoded message.
    std::vector<std::pair<EventHeader, std::string>> TakeMessages() {
        std::vector<std::pair<EventHeader, std::string>> taken;
        std::pair<std::vector<char>, bool> queued;
        while (_out_msgs.try_pop(queued)) {
            std::string whole(queued.first.data(), queued.first.size());
            taken.emplace_back(Message::GetEventHeader(whole), std::move(whole));
        }
        return taken;
    }

    template <typename T>
    std::vector<T> TakeMessagesOfType(CARTA::EventType type) {
        std::vector<T> decoded;
        for (const auto& [header, whole] : TakeMessages()) {
            if (header.GetType() == type) {
                decoded.push_back(Message::DecodeMessage<T>(whole));
            }
        }
        return decoded;
    }

    std::vector<CARTA::RegionHistogramData> TakeHistograms() {
        return TakeMessagesOfType<CARTA::RegionHistogramData>(CARTA::EventType::REGION_HISTOGRAM_DATA);
    }
};

// The process-wide histogram method goes back however a test ends.
struct RestoreHistogramMethod {
    ~RestoreHistogramMethod() {
        ConfigureZarrHistogram("exact");
    }
};

std::shared_ptr<FileLoader> OpenZarrLoader() {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    if (loader) {
        loader->OpenFile("");
    }
    return loader;
}

// Frame keeps its caches to itself, and one test below needs to see whether the cube histogram
// actually landed in one.
class PeekableFrame : public Frame {
public:
    using Frame::Frame;
    using Frame::GetCachedCubeHistogram;
    using Frame::GetCachedImageHistogram;
};

std::shared_ptr<PeekableFrame> ZarrFrame(std::shared_ptr<FileLoader> loader) {
    return std::shared_ptr<PeekableFrame>(new PeekableFrame(0, std::move(loader), ""));
}

// A frame over any format, for the tests that check the cube histogram cache is not a Zarr
// arrangement. A Zarr store is a directory and takes no hdu, and its loader wants opening first;
// FITS and HDF5 take the hdu the other suites pass.
std::shared_ptr<PeekableFrame> FrameOver(const std::filesystem::path& path) {
    auto loader = FileLoader::GetLoader(path.string());
    if (!loader) {
        return nullptr;
    }
    std::string hdu = "0";
    if (std::filesystem::is_directory(path)) {
        hdu.clear();
        loader->OpenFile(hdu);
    }
    return std::shared_ptr<PeekableFrame>(new PeekableFrame(0, std::move(loader), hdu));
}

CARTA::SetHistogramRequirements CubeHistogramRequirements(int file_id, int num_bins) {
    CARTA::SetHistogramRequirements message;
    message.set_file_id(file_id);
    message.set_region_id(CUBE_REGION_ID);
    auto* config = message.add_histograms();
    config->set_coordinate("z");
    config->set_channel(ALL_Z);
    config->set_num_bins(num_bins);
    return message;
}

CARTA::SetHistogramRequirements ImageHistogramRequirements(int file_id, int num_bins, int channel) {
    CARTA::SetHistogramRequirements message;
    message.set_file_id(file_id);
    message.set_region_id(IMAGE_REGION_ID);
    auto* config = message.add_histograms();
    config->set_coordinate("z");
    config->set_channel(channel);
    config->set_num_bins(num_bins);
    return message;
}

// Bytes read() has handed this process, page-cache hits included. A histogram served from a cache
// reads nothing at all, which is a sharper thing to assert than how long it took.
//
// Reading /proc/self/io is itself a read, so the counter moves even across doing nothing; every
// measurement below is taken against that cost rather than against zero.
long long BytesRead() {
    std::ifstream io("/proc/self/io");
    std::string key;
    long long value = 0;
    while (io >> key >> value) {
        if (key == "rchar:") {
            return value;
        }
    }
    return -1;
}

std::int64_t TotalCount(const CARTA::Histogram& histogram) {
    std::int64_t total = 0;
    for (int bin = 0; bin < histogram.bins_size(); ++bin) {
        total += histogram.bins(bin);
    }
    return total;
}

// The last message of a completed cube histogram, which is the one the frontend keeps.
const CARTA::RegionHistogramData* FinalHistogram(const std::vector<CARTA::RegionHistogramData>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->progress() >= 1.0) {
            return &(*it);
        }
    }
    return nullptr;
}

class SessionTest : public ::testing::Test {};

// The harness itself: a session with no transport still opens a file through the ICD path and
// queues the acknowledgement it would have sent.
TEST_F(SessionTest, OpensAZarrStoreThroughTheIcdPath) {
    HeadlessSession session;
    const int file_id = 3;
    auto open = Message::OpenFile(kZarrFixture.parent_path().string(), kZarrFixture.filename().string(), false, "", file_id, false);

    ASSERT_TRUE(session.OnOpenFile(open, 1)) << "the session should open the Zarr fixture";
    EXPECT_EQ(session._frames.count(file_id), 1u);

    auto acks = session.TakeMessagesOfType<CARTA::OpenFileAck>(CARTA::EventType::OPEN_FILE_ACK);
    ASSERT_EQ(acks.size(), 1u) << "opening a file should acknowledge it exactly once";
    EXPECT_TRUE(acks[0].success());
    EXPECT_EQ(acks[0].file_id(), file_id);
}

// The frontend asks for a cube histogram by setting requirements, and the answer comes back as a
// message with progress 1. Everything between is Session's own: the cache miss, the calculation,
// and the send. None of it was covered before this file existed.
TEST_F(SessionTest, CubeHistogramRequirementsProduceAHistogram) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    // What the cube actually holds, found the long way, so the assertion below is not the code
    // under test restating itself.
    BasicStats<float> expected;
    for (int z = 0; z < kDepth; ++z) {
        BasicStats<float> plane;
        ASSERT_TRUE(frame->GetBasicStats(z, 0, plane));
        expected.join(plane);
    }
    ASSERT_GT(expected.num_pixels, 0u);

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);

    auto messages = session.TakeHistograms();
    const auto* final_message = FinalHistogram(messages);
    ASSERT_NE(final_message, nullptr) << "the cube histogram never reported itself complete";
    EXPECT_EQ(final_message->region_id(), CUBE_REGION_ID);
    EXPECT_EQ(final_message->channel(), ALL_Z);
    ASSERT_TRUE(final_message->has_histograms());

    const auto& histogram = final_message->histograms();
    EXPECT_EQ(histogram.num_bins(), num_bins);
    ASSERT_EQ(histogram.bins_size(), num_bins);
    EXPECT_EQ(TotalCount(histogram), static_cast<std::int64_t>(expected.num_pixels))
        << "every pixel the cube holds should land in a bin";
    EXPECT_FLOAT_EQ(histogram.mean(), expected.mean);
    EXPECT_EQ(session._histogram_progress, 1.0);
}

// A request for -1 bins means "decide for me". Session resolves it before handing the number on,
// because the batched paths are given it directly and decline a request for -1 bins. That resolve
// was missing once and the bug lived three commits.
//
// The bin count alone cannot catch it: an unresolved -1 makes the batched path decline, the caller
// falls back to its own per-plane loop, and that loop resolves -1 itself -- so the answer comes out
// with the right number of bins either way and only the speed differs. What separates them is which
// path ran, and the halfway marker says: one pass does not send one, two passes do.
TEST_F(SessionTest, CubeHistogramResolvesAutoBinSizeBeforeOfferingItToTheBatchedPath) {
    HeadlessSession session;
    const int file_id = 0;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    ASSERT_EQ(frame->AutoBinSize(), kAutoBins);
    session.AdoptFrame(file_id, frame);

    RestoreHistogramMethod restore;
    ConfigureZarrHistogram("binned");
    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, AUTO_BIN_SIZE), 1);

    const auto messages = session.TakeHistograms();
    const auto* final_message = FinalHistogram(messages);
    ASSERT_NE(final_message, nullptr);
    ASSERT_TRUE(final_message->has_histograms());
    EXPECT_EQ(final_message->histograms().num_bins(), kAutoBins);
    EXPECT_EQ(final_message->histograms().bins_size(), kAutoBins) << "an auto bin count should reach the bins themselves";

    for (const auto& message : messages) {
        EXPECT_NE(message.progress(), 0.5f)
            << "a halfway marker means the one-pass path declined, which is what an unresolved -1 bins does to it";
    }
}

// The same request with one pass switched off, so that the marker the test above insists on not
// seeing is shown to be a thing this fixture does produce.
TEST_F(SessionTest, TwoPassCubeHistogramDoesSendAHalfwayMarker) {
    HeadlessSession session;
    const int file_id = 0;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, AUTO_BIN_SIZE), 1);

    bool saw_halfway = false;
    for (const auto& message : session.TakeHistograms()) {
        saw_halfway |= (message.progress() == 0.5f);
    }
    EXPECT_TRUE(saw_halfway);
}

// The extension point: a loader that can find the range and bin in one pass answers instead of the
// two loops below it. The point of the seam is that the answer does not change, and this is the
// layer where the choice is actually made, so this is where the two are compared.
TEST_F(SessionTest, OnePassCubeHistogramMatchesTwoPassThroughTheSession) {
    const int file_id = 0;
    const int num_bins = 7;

    auto run = [&](bool one_pass) {
        HeadlessSession session;
        auto loader = OpenZarrLoader();
        EXPECT_NE(loader, nullptr);
        auto frame = ZarrFrame(loader);
        EXPECT_TRUE(frame->IsValid());
        session.AdoptFrame(file_id, frame);

        RestoreHistogramMethod restore;
        ConfigureZarrHistogram(one_pass ? "binned" : "exact");
        session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);

        const auto messages = session.TakeHistograms();
        const auto* final_message = FinalHistogram(messages);
        EXPECT_NE(final_message, nullptr);
        return final_message != nullptr ? final_message->histograms() : CARTA::Histogram();
    };

    const auto two_pass = run(false);
    const auto one_pass = run(true);

    ASSERT_EQ(two_pass.bins_size(), num_bins);
    ASSERT_EQ(one_pass.bins_size(), two_pass.bins_size());
    for (int bin = 0; bin < two_pass.bins_size(); ++bin) {
        EXPECT_EQ(one_pass.bins(bin), two_pass.bins(bin)) << "bin " << bin;
    }
    EXPECT_DOUBLE_EQ(one_pass.bin_width(), two_pass.bin_width());
    EXPECT_DOUBLE_EQ(one_pass.first_bin_center(), two_pass.first_bin_center());
    EXPECT_FLOAT_EQ(one_pass.mean(), two_pass.mean());
    EXPECT_FLOAT_EQ(one_pass.std_dev(), two_pass.std_dev());
}

// A request that fixes the bin edges is one the one-pass walk cannot answer: it finds the range as
// it goes and bins over what it found, so its counts belong to the data's own extremes. Taking them
// anyway published counts of one range under the edges of another -- silently, since the counts
// look like counts.
TEST_F(SessionTest, ACubeHistogramWithFixedBoundsIgnoresTheOnePassWalk) {
    const int file_id = 0;
    const int num_bins = 7;
    const double fixed_min = 0.0;
    const double fixed_max = 4000.0;

    auto run = [&](bool one_pass) {
        HeadlessSession session;
        auto loader = OpenZarrLoader();
        EXPECT_NE(loader, nullptr);
        auto frame = ZarrFrame(loader);
        EXPECT_TRUE(frame->IsValid());
        session.AdoptFrame(file_id, frame);

        auto message = CubeHistogramRequirements(file_id, num_bins);
        auto* config = message.mutable_histograms(0);
        config->set_fixed_bounds(true);
        config->mutable_bounds()->set_min(fixed_min);
        config->mutable_bounds()->set_max(fixed_max);

        RestoreHistogramMethod restore;
        ConfigureZarrHistogram(one_pass ? "binned" : "exact");
        session.OnSetHistogramRequirements(message, 1);

        const auto messages = session.TakeHistograms();
        const auto* final_message = FinalHistogram(messages);
        EXPECT_NE(final_message, nullptr);
        return final_message != nullptr ? final_message->histograms() : CARTA::Histogram();
    };

    const auto two_pass = run(false);
    const auto asked_for_one_pass = run(true);

    ASSERT_EQ(two_pass.bins_size(), num_bins);
    ASSERT_EQ(asked_for_one_pass.bins_size(), num_bins);

    // The edges are the ones that were asked for, in both.
    EXPECT_NEAR(two_pass.bin_width(), (fixed_max - fixed_min) / num_bins, 1e-3);
    EXPECT_NEAR(asked_for_one_pass.bin_width(), (fixed_max - fixed_min) / num_bins, 1e-3)
        << "the counts were binned over the data's range, so these edges do not describe them";
    EXPECT_NEAR(asked_for_one_pass.first_bin_center(), two_pass.first_bin_center(), 1e-3);

    // And so are the counts under them.
    for (int bin = 0; bin < num_bins; ++bin) {
        EXPECT_EQ(asked_for_one_pass.bins(bin), two_pass.bins(bin)) << "bin " << bin;
    }
}

// The one-pass walk reports as it goes, and those reports are supposed to carry the histogram of
// what has been read so far -- which is what the two-pass path sends from its halfway mark on and
// what the frontend re-renders from. Two things make this reachable: a one-byte read budget, so
// the walk takes several reads and reports between them, and a zero progress interval, so the
// session does not sit on those reports for two seconds.
TEST_F(SessionTest, OnePassProgressCarriesTheHistogramSoFar) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    zarr_loader->SetReadBudgetBytes(1);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);
    session._histogram_progress_interval = 0.0;

    RestoreHistogramMethod restore;
    ConfigureZarrHistogram("binned");
    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);

    const auto messages = session.TakeHistograms();
    const auto* final_message = FinalHistogram(messages);
    ASSERT_NE(final_message, nullptr);

    int partials = 0;
    std::int64_t last_total = 0;
    float last_progress = -1.0f;
    for (const auto& message : messages) {
        if (message.progress() >= 1.0) {
            continue;
        }
        EXPECT_GE(message.progress(), last_progress) << "progress should not go backwards";
        last_progress = message.progress();
        if (!message.has_histograms() || message.histograms().bins_size() == 0) {
            continue; // the halfway marker, which carries no bins by design
        }
        ++partials;
        const auto& histogram = message.histograms();
        EXPECT_EQ(histogram.num_bins(), num_bins);
        EXPECT_EQ(histogram.bins_size(), num_bins);
        const auto total = TotalCount(histogram);
        EXPECT_GT(total, 0) << "a partial histogram that carries bins should have counted something";
        EXPECT_GE(total, last_total) << "a partial cannot un-count a pixel";
        EXPECT_LE(total, TotalCount(final_message->histograms())) << "a partial cannot hold more than the whole";
        last_total = total;
    }
    EXPECT_GT(partials, 0) << "a one-byte budget with no progress interval should have reported bins as it went";
}

// The same walk, reported the same way, when the session runs its own two passes. Kept next to the
// one above so that the two paths are held to the same contract rather than only to their own.
TEST_F(SessionTest, TwoPassProgressCarriesTheHistogramSoFar) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);
    session._histogram_progress_interval = 0.0;

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);

    const auto messages = session.TakeHistograms();
    const auto* final_message = FinalHistogram(messages);
    ASSERT_NE(final_message, nullptr);

    bool saw_halfway = false;
    int partials = 0;
    for (const auto& message : messages) {
        if (message.progress() >= 1.0) {
            continue;
        }
        if (message.progress() == 0.5f && message.histograms().bins_size() == 0) {
            saw_halfway = true;
            continue;
        }
        if (message.histograms().bins_size() > 0) {
            ++partials;
            EXPECT_EQ(message.histograms().bins_size(), num_bins);
            EXPECT_LE(TotalCount(message.histograms()), TotalCount(final_message->histograms()));
        }
    }
    EXPECT_TRUE(saw_halfway) << "the two-pass path marks its halfway point";
    EXPECT_GT(partials, 0) << "the second pass should report bins as it accumulates them";
}

// Cancelling means no answer, not a partial one presented as an answer.
TEST_F(SessionTest, CancelledCubeHistogramSendsNoFinalMessage) {
    HeadlessSession session;
    const int file_id = 0;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    std::vector<CARTA::HistogramConfig> configs(1);
    configs[0].set_coordinate("z");
    configs[0].set_channel(ALL_Z);
    configs[0].set_num_bins(7);
    ASSERT_TRUE(frame->SetHistogramRequirements(CUBE_REGION_ID, configs));

    session._histogram_context.cancel_group_execution();
    EXPECT_FALSE(session.SendRegionHistogramData(file_id, CUBE_REGION_ID));

    for (const auto& message : session.TakeHistograms()) {
        EXPECT_LT(message.progress(), 1.0) << "a cancelled histogram should not report itself complete";
    }
}

// A second request is answered from the cache rather than by walking the cube again.
//
// Session caches the finished histogram with Frame::CacheCubeHistogram and the statistics it was
// binned under with CacheCubeStats. Frame::FillRegionHistogramData used to return early for
// CUBE_REGION_ID before it reached either, so the only cache a cube histogram could be served from
// was the loader's own precomputed statistics -- which only HDF5 with a stats sidecar has. Every
// other format re-walked the whole cube on every request, which on a large store is minutes.
TEST_F(SessionTest, ASecondCubeHistogramRequestIsAnsweredFromTheCache) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    // Reporting on every block, so that a walk is loud and a cached answer is a single message.
    session._histogram_progress_interval = 0.0;

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);
    const auto first_messages = session.TakeHistograms();
    const auto* first = FinalHistogram(first_messages);
    ASSERT_NE(first, nullptr);
    const auto first_bins = first->histograms();
    ASSERT_GT(first_messages.size(), 1u) << "the first request walks the cube, so it reports progress";

    EXPECT_TRUE(session.SendRegionHistogramData(file_id, CUBE_REGION_ID));
    const auto second_messages = session.TakeHistograms();
    ASSERT_EQ(second_messages.size(), 1u) << "a cached answer should be one message, not another walk";
    EXPECT_EQ(second_messages[0].progress(), 1.0);
    EXPECT_EQ(second_messages[0].region_id(), CUBE_REGION_ID);
    EXPECT_EQ(second_messages[0].channel(), ALL_Z);

    const auto& cached = second_messages[0].histograms();
    ASSERT_EQ(cached.bins_size(), first_bins.bins_size());
    for (int bin = 0; bin < first_bins.bins_size(); ++bin) {
        EXPECT_EQ(cached.bins(bin), first_bins.bins(bin)) << "bin " << bin;
    }
    EXPECT_DOUBLE_EQ(cached.bin_width(), first_bins.bin_width());
    EXPECT_DOUBLE_EQ(cached.first_bin_center(), first_bins.first_bin_center());
    EXPECT_FLOAT_EQ(cached.mean(), first_bins.mean());
    EXPECT_FLOAT_EQ(cached.std_dev(), first_bins.std_dev());
}

// The same, over FITS, because the cache is Frame's and has nothing to do with Zarr. This is the
// format the early return cost the most: no loader statistics to fall back on.
TEST_F(SessionTest, ASecondCubeHistogramRequestIsAnsweredFromTheCacheForFits) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto frame = FrameOver(FitsImages() / "10x10x10.fits");
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);
    session._histogram_progress_interval = 0.0;

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);
    const auto* first = FinalHistogram(session.TakeHistograms());
    ASSERT_NE(first, nullptr);
    const auto first_bins = first->histograms();
    ASSERT_EQ(first_bins.bins_size(), num_bins);

    EXPECT_TRUE(session.SendRegionHistogramData(file_id, CUBE_REGION_ID));
    const auto second_messages = session.TakeHistograms();
    ASSERT_EQ(second_messages.size(), 1u) << "a cached answer should be one message, not another walk";
    ASSERT_EQ(second_messages[0].histograms().bins_size(), first_bins.bins_size());
    for (int bin = 0; bin < first_bins.bins_size(); ++bin) {
        EXPECT_EQ(second_messages[0].histograms().bins(bin), first_bins.bins(bin)) << "bin " << bin;
    }
}

// The control for the branch above: an HDF5 file with a Statistics/XYZ sidecar answers from the
// loader's own precomputed histogram, on the first request, without walking anything. That path
// runs before the frame cache is consulted and is meant to stay untouched.
TEST_F(SessionTest, AnHdf5CubeHistogramComesFromItsOwnStatisticsWithoutAWalk) {
    HeadlessSession session;
    const int file_id = 0;
    auto frame = FrameOver(Hdf5Images() / "10x10x10.hdf5");
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);
    session._histogram_progress_interval = 0.0;

    // The sidecar decides the bin count, so the request has to be the one that accepts it.
    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, AUTO_BIN_SIZE), 1);

    const auto messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u) << "precomputed statistics should answer in one message, with no walk";
    EXPECT_EQ(messages[0].progress(), 1.0);
    EXPECT_GT(messages[0].histograms().bins_size(), 0);

    // And nothing was cached in the frame, because nothing was calculated.
    BasicStats<float> cube_stats;
    EXPECT_FALSE(frame->GetBasicStats(ALL_Z, 0, cube_stats))
        << "the loader answered, so Session never calculated or cached cube statistics";
}

// The cube cache is keyed the same way, so a different bin count is a miss that walks again rather
// than a hit handed back at the wrong resolution.
TEST_F(SessionTest, ACubeHistogramWithADifferentBinCountIsRecalculated) {
    HeadlessSession session;
    const int file_id = 0;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);
    session._histogram_progress_interval = 0.0;

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, 7), 1);
    const auto* seven = FinalHistogram(session.TakeHistograms());
    ASSERT_NE(seven, nullptr);
    ASSERT_EQ(seven->histograms().bins_size(), 7);
    const auto seven_total = TotalCount(seven->histograms());

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, 13), 2);
    const auto messages = session.TakeHistograms();
    const auto* thirteen = FinalHistogram(messages);
    ASSERT_NE(thirteen, nullptr);
    ASSERT_EQ(thirteen->histograms().bins_size(), 13) << "a different bin count is a cache miss, not a reshaped hit";
    EXPECT_GT(messages.size(), 1u) << "a miss walks the cube, which reports progress";
    EXPECT_EQ(TotalCount(thirteen->histograms()), seven_total) << "same pixels, divided up differently";

    // Both are in the cache now, each under its own bin count.
    BasicStats<float> cube_stats;
    ASSERT_TRUE(frame->GetBasicStats(ALL_Z, 0, cube_stats));
    const HistogramBounds bounds(cube_stats.min_val, cube_stats.max_val);
    Histogram cached;
    EXPECT_TRUE(frame->GetCachedCubeHistogram(0, 7, bounds, cached));
    EXPECT_TRUE(frame->GetCachedCubeHistogram(0, 13, bounds, cached));
}

// The cache the request above cannot reach does work, which is what makes the early return above a
// missed hit rather than a guard against a broken cache.
TEST_F(SessionTest, TheFrameCubeHistogramCacheItselfHoldsTheAnswer) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);
    const auto* final_message = FinalHistogram(session.TakeHistograms());
    ASSERT_NE(final_message, nullptr);

    BasicStats<float> cached_stats;
    ASSERT_TRUE(frame->GetBasicStats(ALL_Z, 0, cached_stats)) << "the cube statistics should have been cached";

    Histogram cached;
    ASSERT_TRUE(frame->GetCachedCubeHistogram(0, num_bins, HistogramBounds(cached_stats.min_val, cached_stats.max_val), cached))
        << "Session cached the finished cube histogram, so the frame should hold it";

    const auto& bins = cached.GetHistogramBins();
    ASSERT_EQ(static_cast<int>(bins.size()), final_message->histograms().bins_size());
    for (std::size_t bin = 0; bin < bins.size(); ++bin) {
        EXPECT_EQ(bins[bin], final_message->histograms().bins(static_cast<int>(bin))) << "bin " << bin;
    }
}

// A plane's histogram is cached by Frame::CalculateHistogram and looked up by
// GetCachedImageHistogram, and Frame::FillRegionHistogramData sits between them -- but nothing
// covered that path. Zeroing what the cache hands back used to pass the whole suite.
//
// The plane asked for is not the current one, so calculating it has to read; a cache hit reads
// nothing. That difference is what separates the two requests, rather than how long each took.
TEST_F(SessionTest, AnImageHistogramIsAnsweredFromTheFrameCacheOnASecondRequest) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    const int channel = 1; // not CurrentZ(), so the first request has to go and read it
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    ASSERT_NE(frame->CurrentZ(), channel);
    session.AdoptFrame(file_id, frame);

    // What the counter moves by across nothing at all.
    const long long idle_a = BytesRead();
    const long long idle_b = BytesRead();
    const long long instrument = idle_b - idle_a;
    ASSERT_GE(instrument, 0);

    const long long before_first = BytesRead();
    session.OnSetHistogramRequirements(ImageHistogramRequirements(file_id, num_bins, channel), 1);
    const long long after_first = BytesRead();
    auto messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u);
    const auto first = messages[0].histograms();
    ASSERT_EQ(first.bins_size(), num_bins);
    EXPECT_EQ(messages[0].channel(), channel);
    ASSERT_GT(after_first - before_first, instrument + 64) << "a plane that is not the current one has to be read";

    // It landed in the cache the second request is supposed to find.
    BasicStats<float> stats;
    ASSERT_TRUE(frame->GetBasicStats(channel, 0, stats));
    Histogram cached;
    ASSERT_TRUE(frame->GetCachedImageHistogram(channel, 0, num_bins, HistogramBounds(stats.min_val, stats.max_val), cached));

    const long long before_second = BytesRead();
    EXPECT_TRUE(session.SendRegionHistogramData(file_id, IMAGE_REGION_ID));
    const long long after_second = BytesRead();
    messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_LE(after_second - before_second, instrument + 8) << "a cached plane histogram should not read anything";

    const auto& repeat = messages[0].histograms();
    ASSERT_EQ(repeat.bins_size(), first.bins_size());
    for (int bin = 0; bin < first.bins_size(); ++bin) {
        EXPECT_EQ(repeat.bins(bin), first.bins(bin)) << "bin " << bin;
    }
    EXPECT_DOUBLE_EQ(repeat.bin_width(), first.bin_width());
    EXPECT_DOUBLE_EQ(repeat.first_bin_center(), first.first_bin_center());
    EXPECT_FLOAT_EQ(repeat.mean(), first.mean());
    EXPECT_FLOAT_EQ(repeat.std_dev(), first.std_dev());
}

// The cache is keyed on the bin count as well as the plane, so asking for a different one is a miss
// that recalculates rather than a hit that hands back the wrong shape.
TEST_F(SessionTest, AnImageHistogramWithADifferentBinCountIsRecalculated) {
    HeadlessSession session;
    const int file_id = 0;
    const int channel = 1;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    session.OnSetHistogramRequirements(ImageHistogramRequirements(file_id, 7, channel), 1);
    auto messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u);
    const auto seven = messages[0].histograms();
    ASSERT_EQ(seven.bins_size(), 7);

    session.OnSetHistogramRequirements(ImageHistogramRequirements(file_id, 13, channel), 2);
    messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u);
    const auto thirteen = messages[0].histograms();
    ASSERT_EQ(thirteen.bins_size(), 13) << "a different bin count is a cache miss, not a reshaped hit";

    // Same pixels either way, however they are divided up.
    EXPECT_EQ(TotalCount(thirteen), TotalCount(seven));
    EXPECT_FLOAT_EQ(thirteen.mean(), seven.mean());
    EXPECT_FLOAT_EQ(thirteen.std_dev(), seven.std_dev());

    // And the first answer is still in the cache, not replaced by the second.
    BasicStats<float> stats;
    ASSERT_TRUE(frame->GetBasicStats(channel, 0, stats));
    const HistogramBounds bounds(stats.min_val, stats.max_val);
    Histogram cached;
    EXPECT_TRUE(frame->GetCachedImageHistogram(channel, 0, 7, bounds, cached));
    EXPECT_TRUE(frame->GetCachedImageHistogram(channel, 0, 13, bounds, cached));
}

// The cache is keyed on the bounds as well, which only a request that fixes them can vary. Without
// this, two histograms of the same plane at the same bin count but over different ranges would be
// the same histogram.
TEST_F(SessionTest, AnImageHistogramWithDifferentFixedBoundsIsRecalculated) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    const int channel = 1;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    auto request = [&](double min, double max, int request_id) {
        auto message = ImageHistogramRequirements(file_id, num_bins, channel);
        auto* config = message.mutable_histograms(0);
        config->set_fixed_bounds(true);
        config->mutable_bounds()->set_min(min);
        config->mutable_bounds()->set_max(max);
        session.OnSetHistogramRequirements(message, request_id);
        auto messages = session.TakeHistograms();
        EXPECT_EQ(messages.size(), 1u);
        return messages.empty() ? CARTA::Histogram() : messages[0].histograms();
    };

    const auto narrow = request(0.0, 1000.0, 1);
    const auto wide = request(0.0, 2000.0, 2);

    ASSERT_EQ(narrow.bins_size(), num_bins);
    ASSERT_EQ(wide.bins_size(), num_bins);
    // The bounds travel as floats, so the widths are compared as floats.
    EXPECT_NEAR(narrow.bin_width(), 1000.0 / num_bins, 1e-3);
    EXPECT_NEAR(wide.bin_width(), 2000.0 / num_bins, 1e-3)
        << "the second range is a cache miss, not the first histogram handed back";
    EXPECT_NE(TotalCount(narrow), TotalCount(wide)) << "a wider range over the same plane holds more pixels";

    // Both are cached, each under its own bounds.
    Histogram cached;
    EXPECT_TRUE(frame->GetCachedImageHistogram(channel, 0, num_bins, HistogramBounds(0.0, 1000.0), cached));
    EXPECT_TRUE(frame->GetCachedImageHistogram(channel, 0, num_bins, HistogramBounds(0.0, 2000.0), cached));
}

// The control for the branch ahead of the frame cache: an HDF5 file with a Statistics/XY sidecar
// answers a plane histogram from the loader, without calculating one and without caching one.
TEST_F(SessionTest, AnHdf5ImageHistogramComesFromItsOwnStatistics) {
    HeadlessSession session;
    const int file_id = 0;
    auto frame = FrameOver(Hdf5Images() / "10x10x10.hdf5");
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    // The sidecar decides the bin count, so the request has to be the one that accepts it.
    session.OnSetHistogramRequirements(ImageHistogramRequirements(file_id, AUTO_BIN_SIZE, CURRENT_Z), 1);

    const auto messages = session.TakeHistograms();
    ASSERT_EQ(messages.size(), 1u);
    const auto& histogram = messages[0].histograms();
    EXPECT_GT(histogram.bins_size(), 0);

    BasicStats<float> stats;
    ASSERT_TRUE(frame->GetBasicStats(frame->CurrentZ(), 0, stats));
    Histogram cached;
    EXPECT_FALSE(frame->GetCachedImageHistogram(
        frame->CurrentZ(), 0, histogram.bins_size(), HistogramBounds(stats.min_val, stats.max_val), cached))
        << "the loader answered, so nothing should have been calculated or cached here";
}

// What a repeated cube histogram request costs, on a store worth measuring. Skipped unless asked:
//
//   CARTA_SESSION_STORE=/path/to/store.zarr CARTA_SESSION_BINS=10000 \
//     ./carta_backend_tests --gtest_filter='SessionTest.MeasureRepeatedCubeHistogram'
TEST_F(SessionTest, MeasureRepeatedCubeHistogram) {
    const char* store = std::getenv("CARTA_SESSION_STORE");
    if (store == nullptr) {
        GTEST_SKIP() << "set CARTA_SESSION_STORE to measure";
    }
    const char* bins_env = std::getenv("CARTA_SESSION_BINS");
    const int num_bins = bins_env != nullptr ? std::atoi(bins_env) : AUTO_BIN_SIZE;

    HeadlessSession session;
    const int file_id = 0;
    auto frame = FrameOver(std::filesystem::path(store));
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    auto timed = [&](const std::function<void()>& work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };

    const double first = timed([&] { session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1); });
    const auto first_messages = session.TakeHistograms();
    const auto* answer = FinalHistogram(first_messages);
    ASSERT_NE(answer, nullptr);

    const double second = timed([&] { EXPECT_TRUE(session.SendRegionHistogramData(file_id, CUBE_REGION_ID)); });
    const auto second_messages = session.TakeHistograms();
    const auto* repeat = FinalHistogram(second_messages);
    ASSERT_NE(repeat, nullptr);

    ASSERT_EQ(repeat->histograms().bins_size(), answer->histograms().bins_size());
    for (int bin = 0; bin < answer->histograms().bins_size(); ++bin) {
        ASSERT_EQ(repeat->histograms().bins(bin), answer->histograms().bins(bin)) << "bin " << bin;
    }

    std::printf("measure: shape %s bins %d -- first %.3f s (%zu messages), repeat %.6f s (%zu messages), %.0fx\n",
        frame->Depth() > 0 ? "cube" : "plane", answer->histograms().bins_size(), first, first_messages.size(), second,
        second_messages.size(), second > 0.0 ? first / second : 0.0);
}

} // namespace
