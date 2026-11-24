/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "ImageGenerators/MomentGenerator.h"
#include "Logger/Logger.h"

#include <spdlog/spdlog.h>

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/TempImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "ImageData/FileLoader.h"
#include "ImageData/Zarr/CartaZarrImage.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "CommonTestUtilities.h"

using namespace carta;

class MomentTest : public ::testing::Test {
public:
    static void GetImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, std::vector<float>& data) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
        casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int stokes_axis = coord_sys.polarizationAxisNumber();

        // Get a slicer
        casacore::IPosition start(image->shape().size());
        start = 0;
        casacore::IPosition end(image->shape());
        end -= 1;

        if (spectral_axis >= 0) {
            start(spectral_axis) = 0;
            end(spectral_axis) = image->shape()[spectral_axis] - 1;
        }
        if (stokes_axis >= 0) {
            start(stokes_axis) = 0;
            end(stokes_axis) = 0;
        }

        // Get image data
        casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
        data.resize(section.length().product());
        casacore::Array<float> tmp(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        casacore::SubImage<float> subimage(*image, section);
        casacore::RO_MaskedLatticeIterator<float> lattice_iter(subimage);

        for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
            casacore::Array<float> cursor_data = lattice_iter.cursor();
            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void CompareImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image1,
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image2) {
        std::vector<float> data1;
        std::vector<float> data2;
        GetImageData(image1, data1);
        GetImageData(image2, data2);
        EXPECT_EQ(data1.size(), data2.size());

        if (data1.size() == data2.size()) {
            for (int i = 0; i < data1.size(); ++i) {
                // Both sides write NaN for a blanked pixel, and EXPECT_FLOAT_EQ fails on NaN. So compare NaN-ness first.
                if (std::isnan(data1[i]) || std::isnan(data2[i])) {
                    EXPECT_EQ(std::isnan(data1[i]), std::isnan(data2[i])) << "at " << i;
                    continue;
                }
                EXPECT_FLOAT_EQ(data1[i], data2[i]) << "at " << i;
            }
        }
    }

    // The twelve moments include coordinate moments, which run the walk on one worker. These leave them out.
    static casacore::Vector<casacore::Int> MomentsWithoutCoordinates() {
        casacore::Vector<casacore::Int> moments(8);
        moments[0] = 0;  // AVERAGE
        moments[1] = 1;  // INTEGRATED
        moments[2] = 4;  // MEDIAN
        moments[3] = 6;  // STANDARD_DEVIATION
        moments[4] = 7;  // RMS
        moments[5] = 8;  // ABS_MEAN_DEVIATION
        moments[6] = 9;  // MAXIMUM
        moments[7] = 11; // MINIMUM
        return moments;
    }

    // `configure` is applied to both generators before they compute. `configure_carta` is applied to carta's only.
    static void GenerateMoments(const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis,
        const casacore::Vector<casacore::Int>& wanted = casacore::Vector<casacore::Int>(),
        const std::function<void(casa::MomentsBase<float>&)>& configure = {},
        const std::function<void(carta::ImageMoments<float>&)>& configure_carta = {}) {
        ASSERT_TRUE(image) << "Image must not be null";
        // create casa/carta moments generators
        casacore::LogOrigin casa_log("casa::ImageMoment", "createMoments", WHERE);
        casacore::LogIO casa_os(casa_log);
        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        casa::ImageMoments<float> casa_image_moments(*image, casa_os, true);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, nullptr, true);

        // set moment types
        casacore::Vector<casacore::Int> moments(12);
        if (!wanted.empty()) {
            moments.resize(wanted.nelements());
            moments = wanted;
        } else {
            moments[0] = 0;   // AVERAGE
            moments[1] = 1;   // INTEGRATED
            moments[2] = 2;   // WEIGHTED_MEAN_COORDINATE
            moments[3] = 3;   // WEIGHTED_DISPERSION_COORDINATE
            moments[4] = 4;   // MEDIAN
            moments[5] = 6;   // STANDARD_DEVIATION
            moments[6] = 7;   // RMS
            moments[7] = 8;   // ABS_MEAN_DEVIATION
            moments[8] = 9;   // MAXIMUM
            moments[9] = 10;  // MAXIMUM_COORDINATE
            moments[10] = 11; // MINIMUM
            moments[11] = 12; // MINIMUM_COORDINATE
        }

        // the other settings
        casacore::Vector<float> include_pix;
        casacore::Vector<float> exclude_pix;
        casacore::Bool do_temp(true);
        casacore::Bool remove_axis(false);

        ASSERT_LT(moments_axis, image->shape().size()) << "Moment axis out of range for image shape";

        if (configure) {
            configure(casa_image_moments);
            configure(carta_image_moments);
        }
        if (configure_carta) {
            configure_carta(carta_image_moments);
        }

        // calculate moments with casa moment generator
        casa_image_moments.setMoments(moments);
        casa_image_moments.setMomentAxis(moments_axis);
        casa_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto casa_results = casa_image_moments.createMoments(do_temp, "casa_image_moments", remove_axis);

        // calculate moments with carta moment generator
        carta_image_moments.setMoments(moments);
        carta_image_moments.setMomentAxis(moments_axis);
        carta_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto carta_results = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        // check the consistency of casa/carta results
        EXPECT_EQ(casa_results.size(), carta_results.size());
        EXPECT_EQ(carta_results.size(), moments.size());

        for (int i = 0; i < casa_results.size(); ++i) {
            auto casa_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(casa_results[i]);
            auto carta_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(carta_results[i]);

            ASSERT_TRUE(casa_moment_image) << "CASA moment image is null at index " << i;
            ASSERT_TRUE(carta_moment_image) << "CARTA moment image is null at index " << i;

            EXPECT_EQ(casa_moment_image->shape().size(), carta_moment_image->shape().size());
            CompareImageData(casa_moment_image, carta_moment_image);
        }
    }
};

TEST_F(MomentTest, CheckConsistency) {
    auto file_path = FitsImages() / "M17_SWex_unittest.fits";
    std::shared_ptr<casacore::ImageInterface<float>> image = std::make_shared<casacore::FITSImage>(file_path.string());
    int moment_axis(2);

    GenerateMoments(image, moment_axis);
}

TEST_F(MomentTest, CheckConsistencyForBeamConvolutions) {
    auto file_path = FitsImages() / "small_perplanebeam.fits";
    std::shared_ptr<casacore::ImageInterface<float>> image = std::make_shared<casacore::FITSImage>(file_path.string());
    int moment_axis(2);

    GenerateMoments(image, moment_axis);
}

// casa::ImageMoments is the oracle. It uses the same collapsers and differs only in how it reads the data.
TEST_F(MomentTest, CheckConsistencyForZarr) {
    const std::filesystem::path fixture{ZARR_PIXEL_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "carta-zarr pixel fixture not found at " << fixture;
    }

    auto loader = FileLoader::GetLoader(fixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<casacore::ImageInterface<float>> image = loader->GetImage();
    ASSERT_NE(image, nullptr);
    ASSERT_EQ(image->shape().size(), 4);

    int moment_axis(2);
    GenerateMoments(image, moment_axis);
}

// Uses the Zarr image's own chunk grid, found as MomentGenerator finds it, on a region whose corner is off the grid.
TEST_F(MomentTest, CheckConsistencyForZarrOnItsChunkGrid) {
    const std::filesystem::path fixture{ZARR_PIXEL_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "carta-zarr pixel fixture not found at " << fixture;
    }

    auto loader = FileLoader::GetLoader(fixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<casacore::ImageInterface<float>> image = loader->GetImage();
    ASSERT_NE(image, nullptr);
    const casacore::IPosition origin(4, 1, 1, 0, 0);
    const casacore::IPosition length(4, image->shape()(0) - 1, image->shape()(1) - 1, image->shape()(2), 1);
    // Cut the region from a copy with its own cache, as MomentGenerator does.
    std::function<std::shared_ptr<void>(std::uint64_t)> hold_cache;
    const auto own = WithCacheOfItsOwn(*image, hold_cache);
    ASSERT_NE(own, nullptr);
    ASSERT_TRUE(hold_cache);
    auto region = std::make_shared<casacore::SubImage<float>>(*own, casacore::Slicer(origin, length));

    casacore::IPosition unit;
    casacore::IPosition corner;
    std::uint64_t chunk_bytes = 0;
    ASSERT_TRUE(ChunkGridOf(*image, *region, unit, corner, chunk_bytes));
    // Ten float32 pixels and the ten flag bytes that the masked read brings.
    EXPECT_EQ(chunk_bytes, 10U * 4 + 10);
    // The fixture is chunked 1 x 1 x 1 x 2 x 5 in time, frequency, polarization, l and m.
    // In CARTA's order x, y, z, stokes, without time, that is 2 x 5 x 1 x 1.
    EXPECT_EQ(unit, casacore::IPosition(4, 2, 5, 1, 1));
    EXPECT_EQ(corner, origin);

    // An image that is not a Zarr image has no chunk grid.
    casacore::TempImage<float> memory(casacore::TiledShape(image->shape()), image->coordinates());
    casacore::SubImage<float> memory_region(memory, casacore::Slicer(origin, length));
    casacore::IPosition none_unit;
    casacore::IPosition none_corner;
    std::uint64_t none_bytes = 0;
    EXPECT_FALSE(ChunkGridOf(memory, memory_region, none_unit, none_corner, none_bytes));
    std::function<std::shared_ptr<void>(std::uint64_t)> no_hold = [](std::uint64_t) { return nullptr; };
    EXPECT_EQ(WithCacheOfItsOwn(memory, no_hold), nullptr);
    EXPECT_FALSE(no_hold);

    // The walk holds a cache for the copy. The image that the session displays never reads through it.
    std::size_t held_bytes = 0;
    const auto* session = dynamic_cast<const CartaZarrImage*>(image.get());
    ASSERT_NE(session, nullptr);
    const auto* copy = dynamic_cast<const CartaZarrImage*>(own.get());
    ASSERT_NE(copy, nullptr);
    const auto watched_hold = [&, hold_cache](std::uint64_t bytes) {
        auto held = hold_cache(bytes);
        held_bytes = copy->OwnCacheBytes().value_or(0);
        EXPECT_FALSE(session->OwnCacheBytes());
        return held;
    };

    GenerateMoments(region, 2, casacore::Vector<casacore::Int>(), {},
        [&](carta::ImageMoments<float>& moments) { moments.SetChunkGrid(unit, corner, watched_hold); });
    EXPECT_GT(held_bytes, 0U) << "the walk read without a cache of its own";
}

namespace {

// An image in memory that records every section read from it. Its copies record into the same list,
// because a SubImage reads through a copy of its parent.
class SliceRecordingImage : public casacore::TempImage<float> {
public:
    SliceRecordingImage(
        const casacore::IPosition& shape, std::shared_ptr<std::vector<casacore::Slicer>> sections, std::function<void()> on_read = {})
        : casacore::TempImage<float>(casacore::TiledShape(shape), casacore::CoordinateUtil::defaultCoords3D()),
          _sections(std::move(sections)),
          _on_read(std::move(on_read)) {}

    casacore::ImageInterface<float>* cloneII() const override {
        return new SliceRecordingImage(*this);
    }

    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override {
        _sections->push_back(section);
        if (_on_read) {
            _on_read();
        }
        return casacore::TempImage<float>::doGetSlice(buffer, section);
    }

private:
    std::shared_ptr<std::vector<casacore::Slicer>> _sections;
    std::function<void()> _on_read;
};

// Returns the sections that one AVERAGE moment of a region reads from its parent. If `unit` is not empty,
// the moment gets `unit` and `origin` as its chunk grid.
std::vector<casacore::Slicer> SectionsRead(const casacore::IPosition& unit, const casacore::IPosition& origin,
    const std::function<std::shared_ptr<void>(std::uint64_t)>& hold_cache = {}, const std::function<void()>& on_read = {},
    std::uint64_t decoded_chunk_bytes = 0) {
    const casacore::IPosition shape(3, 45, 38, 24);
    auto sections = std::make_shared<std::vector<casacore::Slicer>>();
    SliceRecordingImage image(shape, sections, on_read);
    image.put(casacore::Array<float>(shape, 1.0f));
    const casacore::IPosition length(3, 37, 30, 24);
    casacore::SubImage<float> region(image, casacore::Slicer(origin, length));

    casacore::LogOrigin log("carta::ImageMoments", "SectionsRead", WHERE);
    casacore::LogIO os(log);
    carta::ImageMoments<float> moments(region, os, nullptr, true);
    if (!unit.empty()) {
        moments.SetChunkGrid(unit, origin, hold_cache, decoded_chunk_bytes);
    }
    casacore::Vector<casacore::Int> which(1, 0);
    moments.setMoments(which);
    moments.setMomentAxis(2);
    moments.setInExCludeRange(casacore::Vector<float>(), casacore::Vector<float>());
    sections->clear();
    moments.createMoments(true, "sections_read", false);
    return *sections;
}

} // namespace

// An image in memory gives no cursor advice, so without the grid it is read in whole rows by the byte budget.
TEST_F(MomentTest, ToldTheChunksTheSlabIsShapedToThem) {
    const casacore::IPosition unit(3, 8, 7, 4);
    const casacore::IPosition origin(3, 3, 5, 0);

    const auto advised = SectionsRead(casacore::IPosition(), origin);
    ASSERT_FALSE(advised.empty());
    EXPECT_EQ(advised.front().length(), casacore::IPosition(3, 37, 30, 24));

    const auto chunked = SectionsRead(unit, origin);
    ASSERT_FALSE(chunked.empty());
    EXPECT_EQ(chunked.front().start(), origin);
    EXPECT_EQ(chunked.front().length(), casacore::IPosition(3, 8, 7, 24));
    for (const auto& section : chunked) {
        EXPECT_LE(section.length()(0), unit(0)) << "read " << section.start() << " length " << section.length();
        EXPECT_LE(section.length()(1), unit(1)) << "read " << section.start() << " length " << section.length();
    }
}

// Slabs one 8 x 7 chunk across and down, from a corner off the grid, straddle 2 x 2 chunks and all 6 along
// the moment axis: 24 chunks of 8 x 7 x 4 floats.
TEST_F(MomentTest, HoldsTheCacheItsPlanAsksForWhileItWalks) {
    const casacore::IPosition unit(3, 8, 7, 4);
    const casacore::IPosition origin(3, 3, 5, 0);

    std::vector<std::uint64_t> asked;
    std::weak_ptr<void> held;
    int reads = 0;
    int reads_while_held = 0;
    const auto hold = [&](std::uint64_t bytes) -> std::shared_ptr<void> {
        asked.push_back(bytes);
        auto token = std::make_shared<int>(0);
        held = token;
        return token;
    };
    const auto on_read = [&] {
        ++reads;
        reads_while_held += held.expired() ? 0 : 1;
    };

    SectionsRead(unit, origin, hold, on_read);
    ASSERT_EQ(asked.size(), 1U);
    EXPECT_EQ(asked.front(), std::uint64_t(24) * 8 * 7 * 4 * sizeof(float));
    EXPECT_GT(reads, 0);
    EXPECT_EQ(reads_while_held, reads) << "a read was made without the cache held";
    EXPECT_TRUE(held.expired()) << "the walk kept its cache after it was done";

    // Given the decoded chunk size, the cache counts chunks: here float64 with its flag, 8 x 7 x 4 pixels at 9 bytes each.
    asked.clear();
    SectionsRead(unit, origin, hold, {}, std::uint64_t(8) * 7 * 4 * 9);
    ASSERT_EQ(asked.size(), 1U);
    EXPECT_EQ(asked.front(), std::uint64_t(24) * 8 * 7 * 4 * 9);

    asked.clear();
    SectionsRead(casacore::IPosition(), origin, hold);
    EXPECT_TRUE(asked.empty());

    // A chunk larger than the region gives a plan by bytes, which keeps the session's cache.
    SectionsRead(casacore::IPosition(3, 64, 64, 64), origin, hold);
    EXPECT_TRUE(asked.empty());
}

// CartaHdf5Image gives its HDF5 chunk shape as the cursor advice, so the slab is shaped from it. The FITS images
// of the first two tests in this file give no advice finer than the image, so they get the byte budget.
TEST_F(MomentTest, CheckConsistencyForChunkedHdf5) {
    auto file_path = Hdf5Images() / "1000x1000x2x2_nans.hdf5";
    if (!std::filesystem::exists(file_path)) {
        GTEST_SKIP() << "fixture not found at " << file_path;
    }
    auto loader = FileLoader::GetLoader(file_path.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("0");
    std::shared_ptr<casacore::ImageInterface<float>> image = loader->GetImage();
    ASSERT_NE(image, nullptr);
    ASSERT_GE(image->shape().size(), 3);

    int moment_axis(2);
    GenerateMoments(image, moment_axis);
}

// The other comparisons with casa ask for all twelve moments, so they run on one worker. This one leaves out
// the coordinate moments, so it runs the parallel walk.
TEST_F(MomentTest, CheckConsistencyInParallel) {
    auto file_path = Hdf5Images() / "1000x1000x2x2_nans.hdf5";
    if (!std::filesystem::exists(file_path)) {
        GTEST_SKIP() << "fixture not found at " << file_path;
    }
    auto loader = FileLoader::GetLoader(file_path.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("0");
    std::shared_ptr<casacore::ImageInterface<float>> image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    int moment_axis(2);
    GenerateMoments(image, moment_axis, MomentsWithoutCoordinates());
}

// MomentWindow keeps the window of a line in statics, so the walk must run the window method on one worker.
// Each pixel's spectral line peaks at its own channel, so that the lines want different windows.
TEST_F(MomentTest, CheckConsistencyForTheWindowMethod) {
    const casacore::IPosition shape(3, 48, 48, 64);
    auto image = std::make_shared<casacore::TempImage<float>>(casacore::TiledShape(shape), casacore::CoordinateUtil::defaultCoords3D());
    casacore::Array<float> pixels(shape);
    std::uint32_t state = 12345;
    const auto noise = [&state]() {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>(state >> 8) / static_cast<float>(1u << 24)) - 0.5f;
    };
    for (casacore::Int y = 0; y < shape(1); ++y) {
        for (casacore::Int x = 0; x < shape(0); ++x) {
            const float centre = 8.0f + static_cast<float>((x * 7 + y * 13) % 48);
            const float width = 1.5f + static_cast<float>((x + y) % 4);
            for (casacore::Int z = 0; z < shape(2); ++z) {
                const float offset = (static_cast<float>(z) - centre) / width;
                pixels(casacore::IPosition(3, x, y, z)) = 10.0f * std::exp(-0.5f * offset * offset) + noise();
            }
        }
    }
    image->put(pixels);

    casacore::Vector<casacore::Int> window(1, casa::MomentsBase<float>::WINDOW);
    GenerateMoments(image, 2, MomentsWithoutCoordinates(),
        [&](casa::MomentsBase<float>& moments) { ASSERT_TRUE(moments.setWinFitMethod(window)) << moments.errorMessage(); });
}
