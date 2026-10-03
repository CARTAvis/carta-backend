/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Frame/Frame.h"
#include "ImageData/ZarrLoader.h"

#include "CommonTestUtilities.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

// A Zarr loader whose cursor read ends as a test says, which no store on disk can be made to do on
// cue: read as the real one does, decline, fail, or be stopped by the cursor moving on.
class ScriptedCursorLoader : public ZarrLoader {
public:
    enum class Script { read, decline, fail, cursor_moves, stokes_changes };

    ScriptedCursorLoader(const std::string& filename, Script script) : ZarrLoader(filename), _script(script) {}

    BatchOutcome GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const std::function<bool()>& cancellation_requested,
        const std::function<bool(float progress)>& partial_callback) override {
        ++cursor_reads;
        switch (_script) {
            case Script::read:
                return ZarrLoader::GetCursorSpectralData(
                    data, stokes, cursor_x, count_x, cursor_y, count_y, image_mutex, cancellation_requested, partial_callback);
            case Script::decline:
                return BatchOutcome::declined;
            case Script::fail:
                return BatchOutcome::failed;
            case Script::cursor_moves:
                frame->SetCursor(0, 0);
                EXPECT_TRUE(cancellation_requested());
                return BatchOutcome::cancelled;
            case Script::stokes_changes: {
                // The user turns to Q while I is being read, and the read finishes regardless.
                std::string message;
                EXPECT_TRUE(frame->SetImageChannels(frame->CurrentZ(), 1, message)) << message;
                stokes_change_seen = cancellation_requested();
                return ZarrLoader::GetCursorSpectralData(
                    data, stokes, cursor_x, count_x, cursor_y, count_y, image_mutex, {}, partial_callback);
            }
        }
        return BatchOutcome::failed;
    }

    int cursor_reads = 0;
    bool stokes_change_seen = false;
    Frame* frame = nullptr;

private:
    Script _script;
};

struct CursorProfile {
    bool filled = false;
    std::vector<CARTA::SpectralProfileData> messages;
    int cursor_reads = 0;
    bool stokes_change_seen = false;
};

// The spectrum under a cursor at (2, 2) of the pixel fixture, as the frame sends it.
CursorProfile FillCursorProfile(ScriptedCursorLoader::Script script) {
    auto loader = std::make_shared<ScriptedCursorLoader>(kZarrFixture.string(), script);
    loader->OpenFile("");
    Frame frame(0, loader, "");
    loader->frame = &frame;
    frame.SetCursor(2, 2);

    CARTA::SetSpectralRequirements_SpectralConfig config;
    config.set_coordinate("z");
    config.add_stats_types(CARTA::StatsType::Sum);
    EXPECT_TRUE(frame.SetSpectralRequirements(CURSOR_REGION_ID, {config}));

    CursorProfile profile;
    profile.filled =
        frame.FillSpectralProfileData([&](CARTA::SpectralProfileData data) { profile.messages.push_back(data); }, CURSOR_REGION_ID, false);
    profile.cursor_reads = loader->cursor_reads;
    profile.stokes_change_seen = loader->stokes_change_seen;
    return profile;
}

std::vector<float> FinalValues(const CursorProfile& profile) {
    if (profile.messages.empty() || profile.messages.back().progress() < 1.0f || profile.messages.back().profiles_size() != 1) {
        return {};
    }
    const auto& values = profile.messages.back().profiles(0).raw_values_fp32();
    return {reinterpret_cast<const float*>(values.data()), reinterpret_cast<const float*>(values.data() + values.size())};
}

// What the fixture holds at (2, 2).
const std::vector<float> kSpectrum{22.0f, 1022.0f};

class CursorSpectralRoutesTest : public ::testing::Test {
public:
    void SetUp() override {
        if (!std::filesystem::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
    }
};

} // namespace

// The loader's own read is the answer, and nothing else is read.
TEST_F(CursorSpectralRoutesTest, AFinishedReadIsTheProfile) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::read);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

// A loader that does not read this cursor leaves it to the frame, which reads it through casacore
// and gets the same spectrum.
TEST_F(CursorSpectralRoutesTest, ADeclinedReadIsLeftToTheFrame) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::decline);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

// A read the cursor moved away from is not read again: the profile is not wanted any more.
TEST_F(CursorSpectralRoutesTest, AReadTheCursorLeftIsNotReadAgain) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::cursor_moves);
    EXPECT_FALSE(profile.filled);
    EXPECT_TRUE(profile.messages.empty());
}

// A read of the current stokes is of the stokes that was current when it began. One the user turned
// away from is not wanted, and a spectrum of I is not sent as Q's: the frame is told of the change,
// and its own request for Q follows.
TEST_F(CursorSpectralRoutesTest, AReadOfAStokesTheUserLeftIsNotSent) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes);
    EXPECT_TRUE(profile.stokes_change_seen) << "the read should be told to stop";
    for (const auto& message : profile.messages) {
        EXPECT_LT(message.progress(), 1.0f) << "a finished spectrum of stokes " << message.stokes() << " was sent";
    }
}

// A failed read is not read again: the frame would read the same pixels through the same library,
// and meet the same failure.
TEST_F(CursorSpectralRoutesTest, AFailedReadIsNotReadAgain) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::fail);
    EXPECT_FALSE(profile.filled);
    EXPECT_TRUE(profile.messages.empty());
    EXPECT_EQ(profile.cursor_reads, 1);
}

// How each loader's own cursor read ends.

TEST_F(CursorSpectralRoutesTest, AZarrReadDeclinesWhatItCannotRead) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::mutex image_mutex;
    std::vector<float> data;
    EXPECT_EQ(loader->GetCursorSpectralData(data, 0, 2, 1, 2, 1, image_mutex), BatchOutcome::finished);
    EXPECT_EQ(data, kSpectrum);
    EXPECT_EQ(loader->GetCursorSpectralData(data, 7, 2, 1, 2, 1, image_mutex), BatchOutcome::declined) << "no such stokes";
    EXPECT_EQ(loader->GetCursorSpectralData(data, 0, 4, 1, 2, 1, image_mutex), BatchOutcome::declined) << "past the edge";
}

// A chunk that will not decode is the store failing, and what was read of the spectrum is not kept.
TEST_F(CursorSpectralRoutesTest, AZarrReadOfABrokenChunkFails) {
    const auto store = std::filesystem::temp_directory_path() / ("carta_cursor_routes_" + std::to_string(::getpid()));
    std::filesystem::remove_all(store);
    std::filesystem::copy(kZarrFixture, store, std::filesystem::copy_options::recursive);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(store / "SKY" / "c")) {
        if (entry.is_regular_file()) {
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc) << "x";
        }
    }

    auto loader = FileLoader::GetLoader(store.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::mutex image_mutex;
    std::vector<float> data;
    EXPECT_EQ(loader->GetCursorSpectralData(data, 0, 2, 1, 2, 1, image_mutex), BatchOutcome::failed);
    EXPECT_TRUE(data.empty());

    loader.reset();
    std::error_code error;
    std::filesystem::remove_all(store, error);
}

// An HDF5 file reads a spectrum itself only from its swizzled copy, and one without leaves it to the
// frame.
TEST_F(CursorSpectralRoutesTest, AnHdf5ReadIsOnlyOfItsSwizzledCopy) {
    std::mutex image_mutex;
    std::vector<float> data;

    auto swizzled = FileLoader::GetLoader(Hdf5Images() / "10x10x10.hdf5");
    ASSERT_NE(swizzled, nullptr);
    // The frame is what tells the loader its axes.
    Frame swizzled_frame(0, swizzled, "0");
    EXPECT_EQ(swizzled->GetCursorSpectralData(data, 0, 2, 1, 3, 1, image_mutex), BatchOutcome::finished);
    EXPECT_EQ(data.size(), 10u);
    // Past the edge is the pixels having been asked, and the answer not arriving.
    EXPECT_EQ(swizzled->GetCursorSpectralData(data, 0, 20, 1, 3, 1, image_mutex), BatchOutcome::failed);

    auto plain = FileLoader::GetLoader(Hdf5Images() / "10x10x1x10.hdf5");
    ASSERT_NE(plain, nullptr);
    Frame plain_frame(0, plain, "0");
    EXPECT_EQ(plain->GetCursorSpectralData(data, 0, 2, 1, 3, 1, image_mutex), BatchOutcome::declined);
}

// An image with no loader of its own to read spectra -- FITS here -- has every cursor profile read by
// the frame.
TEST_F(CursorSpectralRoutesTest, AnImageWithNoReaderOfItsOwnIsReadByTheFrame) {
    const auto path = FitsImages() / "10x10x10.fits";
    std::shared_ptr<FileLoader> loader(FileLoader::GetLoader(path.string()));
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("0");
    Frame frame(0, loader, "0");
    frame.SetCursor(2, 3);

    CARTA::SetSpectralRequirements_SpectralConfig config;
    config.set_coordinate("z");
    config.add_stats_types(CARTA::StatsType::Sum);
    ASSERT_TRUE(frame.SetSpectralRequirements(CURSOR_REGION_ID, {config}));

    CursorProfile profile;
    profile.filled =
        frame.FillSpectralProfileData([&](CARTA::SpectralProfileData data) { profile.messages.push_back(data); }, CURSOR_REGION_ID, false);
    EXPECT_TRUE(profile.filled);

    FitsDataReader reader(path.string());
    EXPECT_EQ(FinalValues(profile), reader.ReadRegion({2, 3, 0}, {3, 4, 10}));
}
