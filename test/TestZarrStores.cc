/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <casacore/casa/Exceptions/Error.h>

#include "FileList/FileInfoLoader.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/ZarrStores.h"
#include "Util/Casacore.h"

using namespace carta;
using carta::zarr::AxisRole;

namespace fs = std::filesystem;

namespace {

const fs::path kZarrFixture{ZARR_PIXEL_FIXTURE};

std::string ReadText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

} // namespace

class ZarrStoresTest : public ::testing::Test {
public:
    void SetUp() override {
        if (!fs::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
        _scratch = fs::temp_directory_path() / ("carta_zarr_stores_" + std::to_string(::getpid()));
        fs::remove_all(_scratch);
        fs::create_directories(_scratch);
        ForgetEverything();
    }

    void TearDown() override {
        ForgetEverything();
        std::error_code error;
        fs::remove_all(_scratch, error);
    }

    // A copy of the pixel fixture, an image store
    fs::path AStore(const std::string& name = "store.zarr") {
        const auto store = _scratch / name;
        fs::copy(kZarrFixture, store, fs::copy_options::recursive);
        return store;
    }

    // An XRADIO image store without the coordinate system every image needs
    fs::path AMalformedStore() {
        const auto store = AStore("malformed.zarr");
        auto text = ReadText(store / "zarr.json");
        const std::string key = "\"coordinate_system_info\"";
        const auto at = text.find(key);
        EXPECT_NE(at, std::string::npos);
        text.replace(at, key.size(), "\"not_coordinate_system_info\"");
        WriteText(store / "zarr.json", text);
        return store;
    }

    // An XRADIO image store naming no schema version, as XRADIO 1.2.4 and earlier wrote
    fs::path AnUnversionedStore() {
        const auto store = AStore("unversioned.zarr");
        auto text = ReadText(store / "zarr.json");
        const std::string key = "\"schema_version\"";
        const auto at = text.find(key);
        EXPECT_NE(at, std::string::npos);
        text.replace(at, key.size(), "\"not_schema_version\"");
        WriteText(store / "zarr.json", text);
        return store;
    }

    // A valid Zarr v3 group that is not an image
    fs::path AZarrOfSomethingElse() {
        const auto store = _scratch / "other.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", R"({"zarr_format": 3, "node_type": "group", "attributes": {"type": "visibilities"}})");
        return store;
    }

    // Root metadata that does not parse
    fs::path AnUnparseableRoot() {
        const auto store = _scratch / "unparseable.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", "{ this is not json");
        return store;
    }

    static void ForgetEverything() {
        ZarrStores::Instance().Clear();
    }

    // Append to a chunk file several levels below the root, which changes no directory timestamp
    static void GrowAChunk(const fs::path& store, std::size_t bytes) {
        for (const auto& entry : fs::recursive_directory_iterator(store / "SKY" / "c")) {
            if (entry.is_regular_file()) {
                std::ofstream stream(entry.path(), std::ios::binary | std::ios::app);
                stream << std::string(bytes, 'x');
                return;
            }
        }
        ADD_FAILURE() << "no chunk file in " << store;
    }

    static int64_t FileInfoSize(const fs::path& store) {
        CARTA::FileInfo file_info;
        FileInfoLoader(store.string(), CARTA::FileType::ZARR).FillFileInfo(file_info);
        return file_info.size();
    }

    fs::path _scratch;
};

TEST_F(ZarrStoresTest, AStoreIsOpenedOnceAndOffersItsImages) {
    const auto look = ZarrStores::Instance().Look(AStore().string());
    EXPECT_TRUE(look->is_zarr);
    EXPECT_TRUE(look->dataset.has_value());
    ASSERT_EQ(look->offered.size(), 1);
    EXPECT_EQ(look->offered.front(), "SKY");
    EXPECT_TRUE(look->why_not_openable.empty());
}

// A malformed image store is listed as an image, and selecting or opening it says why it is broken
TEST_F(ZarrStoresTest, AMalformedStoreIsAnImageThatSaysWhyItWillNotOpen) {
    const auto store = AMalformedStore().string();

    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_TRUE(look->is_zarr);
    EXPECT_FALSE(look->dataset.has_value());
    EXPECT_NE(look->why_not_openable.find("coordinate_system_info"), std::string::npos) << look->why_not_openable;

    // Listed as an image
    std::string message;
    EXPECT_EQ(FolderImageType(store, message), CARTA::FileType::ZARR);

    // Selecting it says why
    CARTA::FileInfo file_info;
    FileInfoLoader loader(store);
    EXPECT_FALSE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::FileType::ZARR);
    EXPECT_NE(loader.Message().find(look->why_not_openable), std::string::npos) << loader.Message();

    // So does opening it
    try {
        CartaZarrImage image(store);
        FAIL() << "a malformed store opened";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(std::string(error.getMesg()).find(look->why_not_openable), std::string::npos) << error.getMesg();
    }
}

// carta-zarr reads XRADIO image schema 0.0.2 only. An older store is listed as an image, and selecting or opening
// it says what carta-zarr said: which version it reads, and how to write the store again
TEST_F(ZarrStoresTest, AStoreNamingNoSchemaVersionIsAnImageThatSaysWhyItWillNotOpen) {
    const auto store = AnUnversionedStore().string();

    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_TRUE(look->is_zarr);
    EXPECT_FALSE(look->dataset.has_value());
    EXPECT_NE(look->why_not_openable.find("0.0.2"), std::string::npos) << look->why_not_openable;
    EXPECT_NE(look->why_not_openable.find("XRADIO 1.2.5"), std::string::npos) << look->why_not_openable;

    std::string message;
    EXPECT_EQ(FolderImageType(store, message), CARTA::FileType::ZARR);

    CARTA::FileInfo file_info;
    FileInfoLoader loader(store);
    EXPECT_FALSE(loader.FillFileInfo(file_info));
    EXPECT_NE(loader.Message().find(look->why_not_openable), std::string::npos) << loader.Message();

    try {
        CartaZarrImage image(store);
        FAIL() << "a store naming no schema version opened";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(std::string(error.getMesg()).find(look->why_not_openable), std::string::npos) << error.getMesg();
    }
}

TEST_F(ZarrStoresTest, AZarrStoreOfSomethingElseIsNotAnImage) {
    const auto store = AZarrOfSomethingElse().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_FALSE(look->is_zarr);
    EXPECT_FALSE(look->dataset.has_value());

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

// Root metadata that does not parse is not recognised as an image
TEST_F(ZarrStoresTest, AnUnparseableRootIsNotAnImage) {
    const auto store = AnUnparseableRoot().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_FALSE(look->is_zarr);

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

TEST_F(ZarrStoresTest, AFileIsNotAStore) {
    const auto file = _scratch / "zarr.json";
    fs::copy_file(kZarrFixture / "zarr.json", file);
    EXPECT_FALSE(ZarrStores::Instance().Look(file.string())->is_zarr);
    EXPECT_FALSE(ZarrStores::Instance().Look((_scratch / "missing.zarr").string())->is_zarr);
}

TEST_F(ZarrStoresTest, ALookIsKeptAndSharedByEveryoneWhoAsks) {
    ZarrStores stores(4, std::chrono::seconds(30));
    const auto store = AStore().string();

    const auto first = stores.Look(store);
    EXPECT_EQ(stores.Look(store), first);
    // The same path spelled differently
    EXPECT_EQ(stores.Look(store + "/"), first);

    stores.Clear();
    EXPECT_NE(stores.Look(store), first);
}

// A store whose directory timestamp changed is opened again
TEST_F(ZarrStoresTest, AChangedDirectoryIsLookedAtAgain) {
    ZarrStores stores(4, std::chrono::seconds(30));
    const auto store = AStore();

    const auto first = stores.Look(store.string());
    fs::create_directory(store / "NEW_VARIABLE");
    const auto second = stores.Look(store.string());
    EXPECT_NE(second, first);
    EXPECT_EQ(stores.Look(store.string()), second);
}

TEST_F(ZarrStoresTest, ALookIsKeptOnlyForTheTtl) {
    ZarrStores stores(4, std::chrono::seconds(0));
    const auto store = AStore().string();

    const auto first = stores.Look(store);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_NE(stores.Look(store), first);
}

TEST_F(ZarrStoresTest, TheLeastRecentlyUsedLookIsForgotten) {
    ZarrStores stores(2, std::chrono::seconds(30));
    const auto a = AStore("a.zarr").string();
    const auto b = AStore("b.zarr").string();
    const auto c = AStore("c.zarr").string();

    const auto first_a = stores.Look(a);
    const auto first_b = stores.Look(b);
    // Asking about a makes b the oldest.
    EXPECT_EQ(stores.Look(a), first_a);
    stores.Look(c);

    EXPECT_EQ(stores.Look(a), first_a);
    EXPECT_NE(stores.Look(b), first_b);
}

// A file type is sent as its number, so existing numbers keep their meaning
TEST(FileTypeNumbers, ANumberKeepsTheTypeItWasGiven) {
    EXPECT_EQ(static_cast<int>(CARTA::FileType::MIRIAD), 5);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::UNKNOWN), 6);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::ZARR), 7);
}

// Listing, selecting and opening a store measure its size once
TEST_F(ZarrStoresTest, ASizeIsMeasuredOnceForALook) {
    const auto store = AStore();
    CARTA::FileInfo file_info;
    FileInfoLoader loader(store.string());
    EXPECT_TRUE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::FileType::ZARR);
    ASSERT_EQ(file_info.hdu_list_size(), 1);
    EXPECT_EQ(file_info.hdu_list(0), "SKY");
    const int64_t first = file_info.size();
    EXPECT_GT(first, 0);

    // The directory timestamp cannot see this growth, so an unchanged size is the kept one
    GrowAChunk(store, 4096);
    EXPECT_EQ(FileInfoSize(store), first);

    ForgetEverything();
    EXPECT_EQ(FileInfoSize(store), first + 4096);
}

TEST_F(ZarrStoresTest, AMalformedStoreIsNotMeasured) {
    EXPECT_EQ(FileInfoSize(AMalformedStore()), 0);
}

namespace {

// A descriptor with the given axes in the given order, and every coordinate CARTA needs
carta::zarr::ImageDescriptor Described(const std::vector<std::pair<AxisRole, std::uint64_t>>& axes) {
    carta::zarr::ImageDescriptor descriptor;
    const auto name_of = [](AxisRole role) -> std::string {
        switch (role) {
            case AxisRole::spatial_x:
                return "l";
            case AxisRole::spatial_y:
                return "m";
            case AxisRole::spectral:
                return "frequency";
            case AxisRole::polarization:
                return "polarization";
            case AxisRole::time:
                return "time";
            default:
                return "other";
        }
    };
    for (const auto& [role, length] : axes) {
        carta::zarr::AxisDescriptor axis;
        axis.role = role;
        axis.length = length;
        axis.name = name_of(role);
        descriptor.axes.push_back(axis);
    }
    descriptor.direction.emplace();
    descriptor.direction->increment = {-1.0e-4, 1.0e-4};
    descriptor.spectral.emplace();
    descriptor.polarization.emplace();
    return descriptor;
}

// The XRADIO order, with all lengths different
carta::zarr::ImageDescriptor XradioOrder(std::uint64_t times = 1) {
    return Described({{AxisRole::spatial_x, 4}, {AxisRole::spatial_y, 5}, {AxisRole::spectral, 3}, {AxisRole::polarization, 2},
        {AxisRole::time, times}});
}

// An image of the aperture plane: u and v play the spatial roles, and an aperture coordinate replaces the direction
carta::zarr::ImageDescriptor Aperture() {
    auto descriptor = XradioOrder();
    descriptor.axes[0].name = "u";
    descriptor.axes[1].name = "v";
    descriptor.direction.reset();
    descriptor.aperture.emplace();
    descriptor.aperture->increment = {-11459.2, 14323.9};
    return descriptor;
}

// The same image with its axes in another order
carta::zarr::ImageDescriptor Shuffled() {
    return Described(
        {{AxisRole::time, 1}, {AxisRole::polarization, 2}, {AxisRole::spectral, 3}, {AxisRole::spatial_y, 5}, {AxisRole::spatial_x, 4}});
}

} // namespace

TEST(CartaShape, AnImageInTheProfilesOrderIsShapedAsCartaExpects) {
    std::string reason;
    const auto shape = CartaShape(XradioOrder(), reason);
    ASSERT_TRUE(shape.has_value()) << reason;
    EXPECT_EQ(*shape, casacore::IPosition(4, 4, 5, 3, 2));
}

// Reads come back in the image's logical order, so another order would put the pixels in the wrong place
TEST(CartaShape, AnotherOrderIsNotDisplayed) {
    std::string reason;
    EXPECT_FALSE(CartaShape(Shuffled(), reason).has_value());
    EXPECT_NE(reason.find("time, polarization, frequency, m, l"), std::string::npos) << reason;
}

TEST(CartaShape, MoreThanOneTimeIsNotDisplayed) {
    std::string reason;
    EXPECT_FALSE(CartaShape(XradioOrder(2), reason).has_value());
    EXPECT_NE(reason.find("singleton time axis"), std::string::npos) << reason;
}

TEST(CartaShape, AnImageWithoutOneOfCartasFourAxesIsNotDisplayed) {
    std::string reason;
    const auto no_polarization =
        Described({{AxisRole::spatial_x, 4}, {AxisRole::spatial_y, 5}, {AxisRole::spectral, 3}, {AxisRole::time, 1}});
    EXPECT_FALSE(CartaShape(no_polarization, reason).has_value());
    EXPECT_FALSE(reason.empty());
}

TEST(CartaShape, AnAxisCartaHasNoPlaceForIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    carta::zarr::AxisDescriptor extra;
    extra.name = "beam";
    extra.role = AxisRole::other;
    extra.length = 1;
    descriptor.axes.push_back(extra);
    EXPECT_FALSE(CartaShape(descriptor, reason).has_value());
    EXPECT_NE(reason.find("beam"), std::string::npos) << "the reason should name the axis: " << reason;
}

TEST(CartaShape, AMissingCoordinateIsNotDisplayed) {
    for (const auto drop : {0, 1, 2}) {
        auto descriptor = XradioOrder();
        if (drop == 0) {
            descriptor.direction.reset();
        } else if (drop == 1) {
            descriptor.spectral.reset();
        } else {
            descriptor.polarization.reset();
        }
        std::string reason;
        EXPECT_FALSE(CartaShape(descriptor, reason).has_value()) << "coordinate " << drop;
        EXPECT_NE(reason.find("coordinate descriptor"), std::string::npos) << reason;
    }
}

TEST(CartaShape, AnAxisLongerThanCartaCountsIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    descriptor.axes[0].length = std::uint64_t{1} << 32;
    EXPECT_FALSE(CartaShape(descriptor, reason).has_value());
    EXPECT_NE(reason.find("too large"), std::string::npos) << reason;
}

// An empty axis is refused from the axes alone, so that the file list does not offer the image
TEST(CartaShape, AnEmptyAxisIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    descriptor.axes[2].length = 0;
    descriptor.spectral.reset();
    EXPECT_FALSE(CartaShape(descriptor.axes, reason).has_value());
    EXPECT_NE(reason.find("empty axis"), std::string::npos) << reason;
}

// A spatial axis one pixel across has no pixel scale, and is refused from the axes alone
TEST(CartaShape, ASpatialAxisOfOnePixelIsNotDisplayed) {
    for (const std::size_t spatial : {0u, 1u}) {
        auto descriptor = XradioOrder();
        descriptor.axes[spatial].length = 1;
        std::string from_axes;
        std::string from_descriptor;
        EXPECT_FALSE(CartaShape(descriptor.axes, from_axes).has_value()) << descriptor.axes[spatial].name;
        EXPECT_FALSE(CartaShape(descriptor, from_descriptor).has_value()) << descriptor.axes[spatial].name;
        EXPECT_NE(from_axes.find("one pixel"), std::string::npos) << from_axes;
        EXPECT_NE(from_axes.find(descriptor.axes[spatial].name), std::string::npos) << from_axes;
        EXPECT_EQ(from_axes, from_descriptor);
    }
}

TEST(CartaShape, AnApertureImageIsDisplayed) {
    std::string reason;
    const auto shape = CartaShape(Aperture(), reason);
    ASSERT_TRUE(shape.has_value()) << reason;
    EXPECT_EQ(*shape, casacore::IPosition(4, 4, 5, 3, 2));
}

TEST(CartaShape, AnApertureAxisWithNoIncrementIsNotDisplayed) {
    for (const std::size_t axis : {0u, 1u}) {
        auto descriptor = Aperture();
        descriptor.aperture->increment[axis] = 0.0;
        std::string reason;
        EXPECT_FALSE(CartaShape(descriptor, reason).has_value()) << axis;
        EXPECT_NE(reason.find("aperture axis with no increment"), std::string::npos) << reason;
    }
}

// Equal direction cosines leave no increment, which only the descriptor shows
TEST(CartaShape, ADirectionAxisWithNoIncrementIsNotDisplayed) {
    for (const std::size_t axis : {0u, 1u}) {
        auto descriptor = XradioOrder();
        descriptor.direction->increment[axis] = 0.0;
        std::string reason;
        EXPECT_FALSE(CartaShape(descriptor, reason).has_value()) << axis;
        EXPECT_NE(reason.find("no increment"), std::string::npos) << reason;
    }
}

// The listing's axes and the opened image's descriptor give the same answer
TEST(CartaShape, TheAxesAloneAnswerAsTheDescriptorDoes) {
    for (const auto& descriptor : {XradioOrder(), Shuffled(), XradioOrder(2)}) {
        std::string from_axes;
        std::string from_descriptor;
        const auto listed = CartaShape(descriptor.axes, from_axes);
        const auto opened = CartaShape(descriptor, from_descriptor);
        ASSERT_EQ(listed.has_value(), opened.has_value()) << from_axes << " / " << from_descriptor;
        EXPECT_EQ(from_axes, from_descriptor);
        if (listed) {
            EXPECT_EQ(*listed, *opened);
        }
    }
}

namespace {

carta::zarr::ImageEntry Listed(std::string id, bool openable, std::vector<carta::zarr::AxisDescriptor> axes, std::string diagnostic = {}) {
    carta::zarr::ImageEntry entry;
    entry.id = std::move(id);
    entry.openable = openable;
    entry.axes = std::move(axes);
    if (!diagnostic.empty()) {
        carta::zarr::Diagnostic said;
        said.message = std::move(diagnostic);
        entry.diagnostics.push_back(std::move(said));
    }
    return entry;
}

} // namespace

// The offer keeps the library's order and leaves out images either side refuses
TEST(ZarrOffer, TheOfferedImagesAreTheOnesCartaDisplaysInTheLibrarysOrder) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {Listed("APERTURE", false, {}, "an aperture-plane image is not opened"),
        Listed("TWO_TIMES", true, XradioOrder(2).axes), Listed("SKY", true, XradioOrder().axes), Listed("MODEL", true, XradioOrder().axes)};

    const auto offer = OfferedImages(dataset);
    EXPECT_EQ(offer.ids, (std::vector<std::string>{"SKY", "MODEL"}));
    ASSERT_EQ(offer.refused.size(), 2u);
    EXPECT_EQ(offer.refused[0].id, "APERTURE");
    EXPECT_EQ(offer.refused[0].reason, "an aperture-plane image is not opened");
    EXPECT_EQ(offer.refused[1].id, "TWO_TIMES");
    EXPECT_NE(offer.refused[1].reason.find("singleton time axis"), std::string::npos) << offer.refused[1].reason;
    EXPECT_TRUE(offer.why_none.empty()) << "something is offered";
}

// When nothing is offered, CARTA's reason comes first
TEST(ZarrOffer, NothingOfferedSaysCartasReasonBeforeTheLibrarys) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {
        Listed("APERTURE", false, {}, "an aperture-plane image is not opened"), Listed("TWO_TIMES", true, XradioOrder(2).axes)};
    const auto offer = OfferedImages(dataset);
    EXPECT_TRUE(offer.ids.empty());
    EXPECT_NE(offer.why_none.find("singleton time axis"), std::string::npos) << offer.why_none;
}

// With only carta-zarr's refusals, its reason is given
TEST(ZarrOffer, NothingOfferedByTheLibrarySaysTheLibrarysReason) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {Listed("APERTURE", false, {}, "an aperture-plane image is not opened")};
    EXPECT_EQ(OfferedImages(dataset).why_none, "an aperture-plane image is not opened");

    EXPECT_EQ(OfferedImages(carta::zarr::DatasetDescriptor{}).why_none, "XRADIO dataset contains no image variables");
}
