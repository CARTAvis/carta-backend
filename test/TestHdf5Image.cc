/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "Region/RegionHandler.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    FRIEND_TEST(Hdf5ImageTest, ExampleFriendTest);
};

class Hdf5ImageTest : public ::testing::Test {};

TEST_F(Hdf5ImageTest, BasicLoadingTest) {
    auto path = Hdf5Images() / "10x10.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    EXPECT_NE(loader.get(), nullptr);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
}

TEST_F(Hdf5ImageTest, ExampleFriendTest) {
    auto path = Hdf5Images() / "10x10.hdf5";
    // TestFrame used instead of Frame if access to protected values is required
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());
    EXPECT_TRUE(frame->_open_image_error.empty());
}

TEST_F(Hdf5ImageTest, CorrectShape2dImage) {
    auto path = Hdf5Images() / "10x10.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(frame->Depth(), 1);
    EXPECT_EQ(frame->NumStokes(), 1);
}

TEST_F(Hdf5ImageTest, CorrectShape3dImage) {
    auto path = Hdf5Images() / "10x10x10.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 3);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 10);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), -1);
}

TEST_F(Hdf5ImageTest, CorrectShapeDegenerate3dImages) {
    auto path = Hdf5Images() / "10x10x10x1.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 10);
    EXPECT_EQ(shape[3], 1);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), 3);

    // CASA-generated images often have spectral and Stokes axes swapped
    path = Hdf5Images() / "10x10x1x10.hdf5";
    loader = carta::FileLoader::GetLoader(path);
    frame.reset(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 1);
    EXPECT_EQ(shape[3], 10);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), 2);
}

TEST_F(Hdf5ImageTest, CorrectShape4dImages) {
    auto path = Hdf5Images() / "10x10x5x2.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 5);
    EXPECT_EQ(shape[3], 2);
    EXPECT_EQ(frame->Depth(), 5);
    EXPECT_EQ(frame->NumStokes(), 2);
    EXPECT_EQ(frame->StokesAxis(), 3);

    // CASA-generated images often have spectral and Stokes axes swapped
    path = Hdf5Images() / "10x10x2x5.hdf5";
    loader = carta::FileLoader::GetLoader(path);
    frame.reset(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 2);
    EXPECT_EQ(shape[3], 5);
    EXPECT_EQ(frame->Depth(), 5);
    EXPECT_EQ(frame->NumStokes(), 2);
    EXPECT_EQ(frame->StokesAxis(), 2);
}

// A region's spectral profile, which an HDF5 file with a swizzled copy is read for a few columns a call
// and resumed where the last call left off. What it keeps between calls belongs to a region with a
// mask over some channels, and has to be let go of when any of those changes.

namespace {

using ProfilesMap = std::map<CARTA::StatsType, std::vector<double>>;

// The handler keeps the frames it works over to itself.
class Hdf5RegionHandler : public carta::RegionHandler {
public:
    using carta::RegionHandler::_frames;
};

// A region's profile read by the loader to the end.
ProfilesMap LoaderProfile(FileLoader& loader, int region_id, const AxisRange& channels, const casacore::Array<casacore::Bool>& mask,
    const casacore::IPosition& origin) {
    casacore::ArrayLattice<casacore::Bool> lattice(mask);
    std::mutex image_mutex;
    ProfilesMap profile;
    float progress = 0.0f;
    for (int calls = 0; progress < 1.0f && calls < 1000; ++calls) {
        if (!loader.GetRegionSpectralData(region_id, channels, 0, lattice, origin, image_mutex, profile, progress)) {
            return {};
        }
    }
    return profile;
}

// The finite pixels of one channel of a box, straight from the file.
double FinitePixels(const fs::path& path, int x, int y, int width, int height, int channel) {
    Hdf5DataReader reader(path.string());
    const auto pixels = reader.ReadRegion({static_cast<hsize_t>(x), static_cast<hsize_t>(y), static_cast<hsize_t>(channel)},
        {static_cast<hsize_t>(x + width), static_cast<hsize_t>(y + height), static_cast<hsize_t>(channel + 1)});
    return static_cast<double>(std::count_if(pixels.begin(), pixels.end(), [](float pixel) { return std::isfinite(pixel); }));
}

// Two masks over the same 4 x 4 box: every pixel of it, then its first column.
casacore::Array<casacore::Bool> WholeBox() {
    return casacore::Array<casacore::Bool>(casacore::IPosition(2, 4, 4), true);
}

casacore::Array<casacore::Bool> FirstColumn() {
    casacore::Array<casacore::Bool> column(casacore::IPosition(2, 4, 4), false);
    for (int y = 0; y < 4; ++y) {
        column(casacore::IPosition(2, 0, y)) = true;
    }
    return column;
}

} // namespace

// An edit that leaves the region's bounding box alone still makes a different mask of it -- rotating a
// rectangle, dragging one vertex of a polygon inward -- and the loader kept its answer against the box.
TEST_F(Hdf5ImageTest, EditingARegionThroughTheHandlerReachesTheLoader) {
    auto loader = carta::FileLoader::GetLoader(Hdf5Images() / "10x10x10.hdf5");
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    ASSERT_TRUE(frame->IsValid());
    Hdf5RegionHandler handler;
    const int file_id = 0;
    handler._frames[file_id] = frame;

    std::vector<CARTA::Point> control_points{Message::Point(3.5, 3.5), Message::Point(4.0, 4.0)};
    int region_id = -1;
    RegionState rectangle(file_id, CARTA::RegionType::RECTANGLE, control_points, 0.0);
    ASSERT_TRUE(handler.SetRegion(region_id, rectangle, frame->CoordinateSystem()));

    const casacore::IPosition origin(2, 2, 2);
    const auto before = LoaderProfile(*loader, region_id, AxisRange(0, 9), WholeBox(), origin);
    ASSERT_EQ(before.at(CARTA::StatsType::NumPixels).at(0), 16.0);

    // A square turned a quarter keeps its bounding box.
    RegionState turned(file_id, CARTA::RegionType::RECTANGLE, control_points, 90.0);
    ASSERT_TRUE(handler.SetRegion(region_id, turned, frame->CoordinateSystem()));
    const auto after = LoaderProfile(*loader, region_id, AxisRange(0, 9), FirstColumn(), origin);
    EXPECT_EQ(after.at(CARTA::StatsType::NumPixels).at(0), 4.0) << "the profile after the edit should count the pixels of its own mask";
}

TEST_F(Hdf5ImageTest, RemovingARegionThroughTheHandlerReachesTheLoader) {
    auto loader = carta::FileLoader::GetLoader(Hdf5Images() / "10x10x10.hdf5");
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    ASSERT_TRUE(frame->IsValid());
    Hdf5RegionHandler handler;
    const int file_id = 0;
    handler._frames[file_id] = frame;

    std::vector<CARTA::Point> control_points{Message::Point(3.5, 3.5), Message::Point(4.0, 4.0)};
    int region_id = -1;
    RegionState rectangle(file_id, CARTA::RegionType::RECTANGLE, control_points, 0.0);
    ASSERT_TRUE(handler.SetRegion(region_id, rectangle, frame->CoordinateSystem()));

    const casacore::IPosition origin(2, 2, 2);
    ASSERT_EQ(LoaderProfile(*loader, region_id, AxisRange(0, 9), WholeBox(), origin).at(CARTA::StatsType::NumPixels).at(0), 16.0);

    // The next region to be given this id is another region.
    handler.RemoveRegion(region_id);
    EXPECT_EQ(LoaderProfile(*loader, region_id, AxisRange(0, 9), FirstColumn(), origin).at(CARTA::StatsType::NumPixels).at(0), 4.0)
        << "a removed region's profile should not be handed to the next one with its id";
}

// A walk that was not finished -- a PV image cancelled while one of its boxes was being read -- was
// resumed for whatever region next came with the same id, over whatever channels it asked for.
TEST_F(Hdf5ImageTest, AProfileIsNotResumedOverOtherChannels) {
    const auto path = Hdf5Images() / "1000x1000x2_nans.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    ASSERT_TRUE(frame->IsValid());

    // Wider than the columns one call reads, so that the first call leaves the walk unfinished.
    const int width = 20, height = 3;
    casacore::Array<casacore::Bool> box(casacore::IPosition(2, width, height), true);
    casacore::ArrayLattice<casacore::Bool> lattice(box);
    std::mutex image_mutex;
    ProfilesMap left_over;
    float progress = 0.0f;
    ASSERT_TRUE(loader->GetRegionSpectralData(
        TEMP_REGION_ID, AxisRange(0, 0), 0, lattice, casacore::IPosition(2, 100, 100), image_mutex, left_over, progress));
    ASSERT_LT(progress, 1.0f) << "the test needs a walk left unfinished";

    // More channels, somewhere else.
    const auto more = LoaderProfile(*loader, TEMP_REGION_ID, AxisRange(0, 1), box, casacore::IPosition(2, 200, 200));
    ASSERT_EQ(more.at(CARTA::StatsType::NumPixels).size(), 2u) << "a profile of two channels should have two";
    for (int channel = 0; channel < 2; ++channel) {
        EXPECT_EQ(more.at(CARTA::StatsType::NumPixels).at(channel), FinitePixels(path, 200, 200, width, height, channel)) << channel;
    }

    // More channels in the same place, which only the channels tell apart from a continuation.
    ProfilesMap first;
    progress = 0.0f;
    ASSERT_TRUE(loader->GetRegionSpectralData(
        TEMP_REGION_ID, AxisRange(0, 0), 0, lattice, casacore::IPosition(2, 300, 300), image_mutex, first, progress));
    ASSERT_LT(progress, 1.0f);
    const auto both = LoaderProfile(*loader, TEMP_REGION_ID, AxisRange(0, 1), box, casacore::IPosition(2, 300, 300));
    ASSERT_EQ(both.at(CARTA::StatsType::NumPixels).size(), 2u);
    for (int channel = 0; channel < 2; ++channel) {
        EXPECT_EQ(both.at(CARTA::StatsType::NumPixels).at(channel), FinitePixels(path, 300, 300, width, height, channel)) << channel;
    }
}

// Each column read holds every channel of the image, not only those asked for, and a profile of some
// of them read its pixels as though it held only those: the first channel asked for was taken from
// the next row down, and the rest from further on.
TEST_F(Hdf5ImageTest, AProfileOfSomeOfTheChannelsCountsThoseChannels) {
    const auto path = Hdf5Images() / "10x10x10.hdf5";
    auto loader = carta::FileLoader::GetLoader(path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    ASSERT_TRUE(frame->IsValid());

    // The copy a profile is read from, which in this fixture is not the plane data: x, then y, then
    // the channel varying fastest.
    H5::H5File file(path.string(), H5F_ACC_RDONLY);
    auto swizzled = file.openDataSet("/0/PermutedData/ZYX");
    const auto sum_of = [&](int channel) {
        const hsize_t start[3] = {2, 5, static_cast<hsize_t>(channel)};
        const hsize_t count[3] = {4, 3, 1};
        auto file_space = swizzled.getSpace();
        file_space.selectHyperslab(H5S_SELECT_SET, count, start);
        hsize_t size = 12;
        std::vector<float> pixels(size);
        H5::DataSpace memory_space(1, &size);
        swizzled.read(pixels.data(), H5::PredType::NATIVE_FLOAT, memory_space, file_space);
        double sum = 0.0;
        for (const float pixel : pixels) {
            sum += pixel;
        }
        return sum;
    };
    casacore::Array<casacore::Bool> box(casacore::IPosition(2, 4, 3), true);
    int region_id = 1;
    for (const auto& channels : {AxisRange(0, 0), AxisRange(3, 5), AxisRange(9, 9)}) {
        const auto profile = LoaderProfile(*loader, region_id++, channels, box, casacore::IPosition(2, 2, 5));
        const auto count = static_cast<std::size_t>(channels.to - channels.from + 1);
        ASSERT_EQ(profile.at(CARTA::StatsType::Sum).size(), count);
        for (std::size_t c = 0; c < count; ++c) {
            const auto channel = channels.from + static_cast<int>(c);
            EXPECT_NEAR(profile.at(CARTA::StatsType::Sum).at(c), sum_of(channel), 1e-5) << "channel " << channel;
        }
    }
}
