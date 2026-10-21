/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Frame/Frame.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/ZarrLoader.h"

#include "CommonTestUtilities.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

// A Zarr image that counts the reads casacore makes of it, which is how the frame reads a spectrum when the
// loader does not
class CountingImage : public CartaZarrImage {
public:
    CountingImage(const CartaZarrImage& image, int& reads, std::function<void()>& on_read)
        : CartaZarrImage(image), _reads(reads), _on_read(on_read) {}

    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override {
        ++_reads;
        // Once, at the next read
        if (auto once = std::exchange(_on_read, nullptr)) {
            once();
        }
        return CartaZarrImage::doGetSlice(buffer, section);
    }
    // casacore's SubImage reads a clone, which must count too
    casacore::ImageInterface<float>* cloneII() const override {
        return new CountingImage(*this, _reads, _on_read);
    }

private:
    int& _reads;
    std::function<void()>& _on_read;
};

// A Zarr loader whose cursor read follows the script a test gives it: read as the real loader does, return
// false without reading, or be stopped by the frame because the cursor or the stokes changed during the read.
class ScriptedCursorLoader : public ZarrLoader {
public:
    enum class Script { read, refuse, cursor_moves, stokes_changes, stokes_changes_while_casacore_reads };

    ScriptedCursorLoader(const std::string& filename, Script script) : ZarrLoader(filename), _script(script) {}

    bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
        std::mutex& image_mutex, const SpectralReadProgress& progress) override {
        ++cursor_reads;
        switch (_script) {
            case Script::read:
                return ZarrLoader::GetCursorSpectralData(data, stokes, cursor_x, count_x, cursor_y, count_y, image_mutex, progress);
            case Script::refuse:
                return false;
            case Script::cursor_moves:
                frame->SetCursor(0, 0);
                progress_answer = progress(0, 2);
                return false;
            case Script::stokes_changes: {
                // The user turns to Q while I is being read
                std::string message;
                EXPECT_TRUE(frame->SetImageChannels(frame->CurrentZ(), 1, message)) << message;
                // The frame reads the new stokes's plane; only reads after this are of the profile
                casacore_reads = 0;
                progress_answer = progress(0, 2);
                return false;
            }
            case Script::stokes_changes_while_casacore_reads:
                // The user turns to Q while the frame reads I through casacore
                on_casacore_read = [this] {
                    std::string message;
                    EXPECT_TRUE(frame->SetImageChannels(frame->CurrentZ(), 1, message)) << message;
                };
                return false;
        }
        return false;
    }

    int cursor_reads = 0;
    int casacore_reads = 0;
    bool progress_answer = true;
    Frame* frame = nullptr;
    std::function<void()> on_casacore_read;

    // A stokes change during a casacore read must not reload the plane, which would wait for the image mutex
    // the read holds. With a tile cache and mips, the frame loads tiles when asked instead.
    bool UseTileCache() const override {
        return _script == Script::stokes_changes_while_casacore_reads || ZarrLoader::UseTileCache();
    }
    bool HasMip(int mip) const override {
        return (_script == Script::stokes_changes_while_casacore_reads && mip == 2) || ZarrLoader::HasMip(mip);
    }

protected:
    void AllocateImage(const std::string& hdu) override {
        ZarrLoader::AllocateImage(hdu);
        if (auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image); image && !std::dynamic_pointer_cast<CountingImage>(_image)) {
            _image = std::make_shared<CountingImage>(*image, casacore_reads, on_casacore_read);
        }
    }

private:
    Script _script;
};

struct CursorProfile {
    bool filled = false;
    std::vector<CARTA::SpectralProfileData> messages;
    int cursor_reads = 0;
    int casacore_reads = 0;
    bool progress_answer = true;
};

// The spectrum under a cursor at (2, 2) of the pixel fixture, as the frame sends it, of the current stokes
// ("z") or a fixed one
CursorProfile FillCursorProfile(ScriptedCursorLoader::Script script, const std::string& coordinate = "z") {
    auto loader = std::make_shared<ScriptedCursorLoader>(kZarrFixture.string(), script);
    loader->OpenFile("");
    Frame frame(0, loader, "");
    loader->frame = &frame;
    frame.SetCursor(2, 2);

    CARTA::SetSpectralRequirements_SpectralConfig config;
    config.set_coordinate(coordinate);
    config.add_stats_types(CARTA::StatsType::Sum);
    EXPECT_TRUE(frame.SetSpectralRequirements(CURSOR_REGION_ID, {config}));

    // Only the reads made for the profile: the frame reads the plane on show when it is made
    loader->casacore_reads = 0;
    CursorProfile profile;
    profile.filled =
        frame.FillSpectralProfileData([&](CARTA::SpectralProfileData data) { profile.messages.push_back(data); }, CURSOR_REGION_ID, false);
    profile.cursor_reads = loader->cursor_reads;
    profile.casacore_reads = loader->casacore_reads;
    profile.progress_answer = loader->progress_answer;
    return profile;
}

std::vector<float> FinalValues(const CursorProfile& profile) {
    if (profile.messages.empty() || profile.messages.back().progress() < 1.0f || profile.messages.back().profiles_size() != 1) {
        return {};
    }
    const auto& values = profile.messages.back().profiles(0).raw_values_fp32();
    return {reinterpret_cast<const float*>(values.data()), reinterpret_cast<const float*>(values.data() + values.size())};
}

// What the fixture holds at (2, 2) of the first stokes
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

// The loader's read is the profile, and nothing else is read
TEST_F(CursorSpectralRoutesTest, TheLoadersReadIsTheProfile) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::read);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

// A loader that returns false without being stopped leaves the spectrum to the frame, which reads it
// through casacore
TEST_F(CursorSpectralRoutesTest, AReadThatReturnsFalseIsLeftToTheFrame) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::refuse);
    EXPECT_TRUE(profile.filled);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

// The frame stops a read once the cursor has moved, and sends nothing for it
TEST_F(CursorSpectralRoutesTest, AReadTheCursorLeftIsStopped) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::cursor_moves);
    EXPECT_FALSE(profile.progress_answer);
    EXPECT_FALSE(profile.filled);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}

// The frame stops a read of the current stokes once the user turns to another, and neither sends it as the
// new stokes's profile nor reads it again through casacore
TEST_F(CursorSpectralRoutesTest, AReadOfAStokesTheUserLeftIsStopped) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes);
    EXPECT_FALSE(profile.progress_answer);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}

// A profile of a fixed stokes is still wanted when the user turns to another, and is read through casacore
// once the loader leaves it
TEST_F(CursorSpectralRoutesTest, AFixedStokesProfileOutlivesAStokesChange) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes, "Qz");
    EXPECT_TRUE(profile.progress_answer);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), (std::vector<float>{122.0f, 1122.0f}));
}

// The frame's own casacore read of the current stokes also stops when the user turns to another, rather than
// sending it as the new stokes's profile
TEST_F(CursorSpectralRoutesTest, ACasacoreReadOfAStokesTheUserLeftIsNotSent) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes_while_casacore_reads);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}
