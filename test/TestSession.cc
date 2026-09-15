/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
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
};

std::shared_ptr<PeekableFrame> ZarrFrame(std::shared_ptr<FileLoader> loader) {
    return std::shared_ptr<PeekableFrame>(new PeekableFrame(0, std::move(loader), ""));
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

// A second request walks the cube again, which is worth pinning because it is not what the code
// around it reads as. Session caches the finished histogram with Frame::CacheCubeHistogram, and
// Frame::GetCachedCubeHistogram would hand it back -- but Frame::FillRegionHistogramData returns
// early for CUBE_REGION_ID before it ever reaches the frame cache, so the only cache a cube
// histogram can be served from is the loader's own precomputed statistics, which only HDF5 with a
// stats sidecar has. That early return is upstream (Frame.cc, 2020), not part of the Zarr work, so
// it is recorded here rather than changed: on a large cube this is a full re-walk per request.
//
// If that early return is ever fixed, this test is the one that should fail first.
TEST_F(SessionTest, ASecondCubeHistogramRequestWalksTheCubeAgain) {
    HeadlessSession session;
    const int file_id = 0;
    const int num_bins = 7;
    auto loader = OpenZarrLoader();
    ASSERT_NE(loader, nullptr);
    auto frame = ZarrFrame(loader);
    ASSERT_TRUE(frame->IsValid());
    session.AdoptFrame(file_id, frame);

    // Reporting on every block, so that a walk is loud and a cached answer would be silent.
    session._histogram_progress_interval = 0.0;

    session.OnSetHistogramRequirements(CubeHistogramRequirements(file_id, num_bins), 1);
    const auto first_messages = session.TakeHistograms();
    const auto* first = FinalHistogram(first_messages);
    ASSERT_NE(first, nullptr);
    const auto first_bins = first->histograms();
    ASSERT_GT(first_messages.size(), 1u) << "the first request walks the cube, so it reports progress";

    EXPECT_TRUE(session.SendRegionHistogramData(file_id, CUBE_REGION_ID));
    const auto second_messages = session.TakeHistograms();
    const auto* second = FinalHistogram(second_messages);
    ASSERT_NE(second, nullptr);

    EXPECT_GT(second_messages.size(), 1u)
        << "a cache hit would be a single message; more than one means the cube was walked again";

    // Whatever it costs, it has to be the same answer.
    ASSERT_EQ(second->histograms().bins_size(), first_bins.bins_size());
    for (int bin = 0; bin < first_bins.bins_size(); ++bin) {
        EXPECT_EQ(second->histograms().bins(bin), first_bins.bins(bin)) << "bin " << bin;
    }
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

} // namespace
