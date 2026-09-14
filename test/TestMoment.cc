/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "Logger/Logger.h"

#include <spdlog/spdlog.h>

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "ImageData/FileLoader.h"
#include "ImageData/ZarrContext.h"
#include "Main/ProgramSettings.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

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
                // Two implementations that agree on a blanked pixel both say NaN, and
                // EXPECT_FLOAT_EQ calls that a failure. Compare NaN-ness first so that a
                // masked image can be used as an oracle at all, and still fail when only
                // one side blanks.
                if (std::isnan(data1[i]) || std::isnan(data2[i])) {
                    EXPECT_EQ(std::isnan(data1[i]), std::isnan(data2[i])) << "at " << i;
                    continue;
                }
                EXPECT_FLOAT_EQ(data1[i], data2[i]) << "at " << i;
            }
        }
    }

    // The twelve include coordinate moments, which pin the walk to one worker. Anything that
    // means to exercise the parallel walk has to ask for moments that do not.
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

    static void GenerateMoments(const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis,
        const casacore::Vector<casacore::Int>& wanted = casacore::Vector<casacore::Int>()) {
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

// The moment path reads a Zarr cube through casacore's iterator, not through any of the
// loader overrides, so nothing else in the suite covers it. casa::ImageMoments is the
// oracle: it shares the collapsers with carta's fork and differs only in how the data is
// fetched, which is exactly what changes when the read geometry changes.
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

// CartaHdf5Image reports its HDF5 chunk shape as the cursor advice, so the walk shapes its slab
// from that -- the same branch the Zarr case takes, on a different chunking and a different
// loader. casacore's FITSImage, which the two tests above use, reports no tiling finer than the
// fixture itself and keeps the byte-budget shape instead.
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

// ---------------------------------------------------------------------------
// Measurement, not a test. Skipped unless CARTA_MOMENT_STORE names a store.
//
//   CARTA_MOMENT_STORE=<path>     the image to walk
//   CARTA_MOMENT_CHANNELS=<n>     use only the first n channels (default: all)
//   CARTA_MOMENT_XY=<w>x<h>       use only a w x h corner (default: all)
//
// Reports wall time next to what the process actually read, because the thing
// under suspicion is read amplification: the walk asks for a full-depth pencil
// and gets whole chunks back.
// ---------------------------------------------------------------------------
namespace {

struct IoCounters {
    long long rchar = 0;      // bytes handed to the process by read()
    long long read_bytes = 0; // bytes actually fetched from the block device
};

IoCounters ReadIoCounters() {
    IoCounters counters;
    std::ifstream io("/proc/self/io");
    std::string key;
    long long value = 0;
    while (io >> key >> value) {
        if (key == "rchar:") {
            counters.rchar = value;
        } else if (key == "read_bytes:") {
            counters.read_bytes = value;
        }
    }
    return counters;
}

std::string FromEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

} // namespace

TEST_F(MomentTest, MeasureZarrWalk) {
    const std::string store = FromEnv("CARTA_MOMENT_STORE");
    if (store.empty()) {
        GTEST_SKIP() << "set CARTA_MOMENT_STORE to measure";
    }

    // Match what Main.cc does at startup. Without this the shared context is built from a
    // default OpenOptions, which leaves TensorStore's cache pool at its own default of
    // total_bytes_limit 0 -- no decoded-chunk cache at all. A walk that revisits chunks
    // measured against that is measuring a configuration the server never runs in.
    const std::string cache = FromEnv("CARTA_MOMENT_CACHE_MB");
    const int cache_mb = cache.empty() ? ZARR_CACHE_POOL_MB : std::stoi(cache);
    const int threads = omp_get_num_procs();
    carta::ConfigureZarrContext(ZARR_FILE_IO_CONCURRENCY, ZARR_DATA_COPY_CONCURRENCY, cache_mb, threads);

    auto loader = FileLoader::GetLoader(store);
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<casacore::ImageInterface<float>> image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const casacore::IPosition full = image->shape();
    ASSERT_EQ(full.size(), 4);

    casacore::IPosition start(4, 0);
    casacore::IPosition length(full);
    length(3) = 1; // one polarization

    const std::string channels = FromEnv("CARTA_MOMENT_CHANNELS");
    if (!channels.empty()) {
        length(2) = std::min<ssize_t>(full(2), std::stoll(channels));
    }
    const std::string xy = FromEnv("CARTA_MOMENT_XY");
    if (!xy.empty()) {
        const auto cross = xy.find('x');
        ASSERT_NE(cross, std::string::npos) << "CARTA_MOMENT_XY wants <w>x<h>";
        length(0) = std::min<ssize_t>(full(0), std::stoll(xy.substr(0, cross)));
        length(1) = std::min<ssize_t>(full(1), std::stoll(xy.substr(cross + 1)));
    }

    casacore::Slicer section(start, length);
    auto sub = std::make_shared<casacore::SubImage<float>>(*image, section);

    const double wanted_bytes =
        static_cast<double>(length.product()) * static_cast<double>(sizeof(float));

    casacore::LogOrigin origin("carta::ImageMoments", "measure", WHERE);
    casacore::LogIO os(origin);
    carta::ImageMoments<float> moments(*sub, os, nullptr, true);

    casacore::Vector<casacore::Int> which(1);
    which[0] = 0; // AVERAGE -- the cheapest collapser, so the number is the walk
    ASSERT_TRUE(moments.setMoments(which)) << moments.errorMessage();
    ASSERT_TRUE(moments.setMomentAxis(2)) << moments.errorMessage();
    moments.setInExCludeRange(casacore::Vector<float>(), casacore::Vector<float>());

    spdlog::set_level(spdlog::level::debug);
    spdlog::info("measure: shape {} nice {} masked {} multiple_beams {}", sub->shape().toString(),
        sub->niceCursorShape().toString(), sub->isMasked(), sub->imageInfo().hasMultipleBeams());
    const auto before = ReadIoCounters();
    const auto t0 = std::chrono::steady_clock::now();
    auto results = moments.createMoments(true, "moment_measure", false);
    const auto t1 = std::chrono::steady_clock::now();
    const auto after = ReadIoCounters();

    ASSERT_EQ(results.size(), 1u);
    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const double rchar = static_cast<double>(after.rchar - before.rchar);
    const double disk = static_cast<double>(after.read_bytes - before.read_bytes);
    const double mib = 1024.0 * 1024.0;

    std::printf(
        "\nMOMENT store=%s shape=%lldx%lldx%lldx%lld seconds=%.2f wanted_MiB=%.1f "
        "rchar_MiB=%.1f disk_MiB=%.1f amplification=%.1fx\n",
        store.c_str(), (long long)length(0), (long long)length(1), (long long)length(2),
        (long long)length(3), cache_mb, seconds, wanted_bytes / mib, rchar / mib, disk / mib,
        wanted_bytes > 0 ? rchar / wanted_bytes : 0.0);
    std::fflush(stdout);
}

// The walk runs on many workers only when no coordinate moment is asked for, so the tests above --
// which ask for all twelve -- run it on one. This is the same oracle over the same fixture with the
// coordinate moments left out, which is the only case that reaches the parallel path.
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
