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
#include "ImageData/Zarr/CartaZarrImage.h"
#include "ImageData/Zarr/ZarrLoader.h"

#include "CommonTestUtilities.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

// A Zarr image that counts the frame's own reads. The loader's cursor read has a progress callback, and is not counted.
class CountingImage : public CartaZarrImage {
public:
    CountingImage(const CartaZarrImage& image, int& reads, std::function<void()>& on_read)
        : CartaZarrImage(image), _reads(reads), _on_read(on_read) {}

    carta::zarr::Result<std::size_t> Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
        const carta::zarr::ReadOptions& options = {}, const carta::zarr::ProgressCallback& progress = {}) const override {
        if (!progress) {
            ++_reads;
            // Run it once, at the next read
            if (auto once = std::exchange(_on_read, nullptr)) {
                once();
            }
        }
        return CartaZarrImage::Read(buffer, section, options, progress);
    }
    // casacore's SubImage reads a clone, so the clone must count too
    casacore::ImageInterface<float>* cloneII() const override {
        return new CountingImage(*this, _reads, _on_read);
    }

private:
    int& _reads;
    std::function<void()>& _on_read;
};

// A Zarr loader whose cursor read follows a script from the test
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
                // The frame reads the plane of the new stokes. Only the reads after this one are for the profile.
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

    // With a tile cache and mips, a stokes change does not reload the plane. A reload would wait for the image mutex.
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

// The spectrum that the frame sends for a cursor at (2, 2)
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

    // Count only the reads for the profile. The frame already read the plane when it was made, to show it.
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

TEST_F(CursorSpectralRoutesTest, TheLoadersReadIsTheProfile) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::read);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

TEST_F(CursorSpectralRoutesTest, AReadThatReturnsFalseIsLeftToTheFrame) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::refuse);
    EXPECT_TRUE(profile.filled);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), kSpectrum);
}

TEST_F(CursorSpectralRoutesTest, AReadTheCursorLeftIsStopped) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::cursor_moves);
    EXPECT_FALSE(profile.progress_answer);
    EXPECT_FALSE(profile.filled);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}

// The stopped profile is not read again through casacore.
TEST_F(CursorSpectralRoutesTest, AReadOfAStokesTheUserLeftIsStopped) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes);
    EXPECT_FALSE(profile.progress_answer);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_EQ(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}

TEST_F(CursorSpectralRoutesTest, AFixedStokesProfileOutlivesAStokesChange) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes, "Qz");
    EXPECT_TRUE(profile.progress_answer);
    EXPECT_TRUE(profile.filled);
    EXPECT_EQ(profile.cursor_reads, 1);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_EQ(FinalValues(profile), (std::vector<float>{122.0f, 1122.0f}));
}

TEST_F(CursorSpectralRoutesTest, ACasacoreReadOfAStokesTheUserLeftIsNotSent) {
    const auto profile = FillCursorProfile(ScriptedCursorLoader::Script::stokes_changes_while_casacore_reads);
    EXPECT_GT(profile.casacore_reads, 0);
    EXPECT_TRUE(profile.messages.empty());
}
