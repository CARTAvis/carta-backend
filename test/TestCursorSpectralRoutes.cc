/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "Frame/Frame.h"
#include "ImageData/ZarrLoader.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

// A Zarr loader whose cursor read ends as a test says, which no store on disk can be made to do on
// cue: read as the real one does, decline, fail, or be stopped by the cursor moving on.
class ScriptedCursorLoader : public ZarrLoader {
public:
    enum class Script { read, decline, fail, cursor_moves };

    ScriptedCursorLoader(const std::string& filename, Script script) : ZarrLoader(filename), _script(script) {}

    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const std::function<bool()>& cancellation_requested,
        const std::function<bool(float progress)>& partial_callback) override {
        ++cursor_reads;
        switch (_script) {
            case Script::read:
                return ZarrLoader::GetCursorSpectralData(
                    data, stokes, cursor_x, count_x, cursor_y, count_y, image_mutex, cancellation_requested, partial_callback);
            case Script::decline:
                return false;
            case Script::fail:
                data.clear();
                return false;
            case Script::cursor_moves:
                frame->SetCursor(0, 0);
                EXPECT_TRUE(cancellation_requested());
                data.clear();
                return false;
        }
        return false;
    }

    int cursor_reads = 0;
    Frame* frame = nullptr;

private:
    Script _script;
};

struct CursorProfile {
    bool filled = false;
    std::vector<CARTA::SpectralProfileData> messages;
    int cursor_reads = 0;
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

// Today a failed read is read again by the frame, through the same library.
TEST_F(CursorSpectralRoutesTest, AFailedReadIsReadAgainByTheFrame) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::fail);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}
