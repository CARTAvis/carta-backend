/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
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
    CountingImage(const CartaZarrImage& image, int& reads) : CartaZarrImage(image), _reads(reads) {}

    carta::zarr::Result<std::size_t> Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
        const carta::zarr::ReadOptions& options = {}, const carta::zarr::ProgressCallback& progress = {}) const override {
        if (!progress) {
            ++_reads;
        }
        return CartaZarrImage::Read(buffer, section, options, progress);
    }
    // casacore's SubImage reads a clone, so the clone must count too
    casacore::ImageInterface<float>* cloneII() const override {
        return new CountingImage(*this, _reads);
    }

private:
    int& _reads;
};

// A Zarr loader whose cursor read follows a script from the test
class ScriptedCursorLoader : public ZarrLoader {
public:
    enum class Script { read, refuse, cursor_moves };

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
        }
        return false;
    }

    int cursor_reads = 0;
    int casacore_reads = 0;
    bool progress_answer = true;
    Frame* frame = nullptr;

protected:
    void AllocateImage(const std::string& hdu) override {
        ZarrLoader::AllocateImage(hdu);
        if (auto image = std::dynamic_pointer_cast<CartaZarrImage>(_image); image && !std::dynamic_pointer_cast<CountingImage>(_image)) {
            _image = std::make_shared<CountingImage>(*image, casacore_reads);
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
