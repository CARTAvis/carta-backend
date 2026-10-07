/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ImageData/CartaZarrAxes.h"

using carta::CartaZarrAxes;
using carta::zarr::AxisRole;

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

// The same image with its axes in another order
carta::zarr::ImageDescriptor Shuffled() {
    return Described(
        {{AxisRole::time, 1}, {AxisRole::polarization, 2}, {AxisRole::spectral, 3}, {AxisRole::spatial_y, 5}, {AxisRole::spatial_x, 4}});
}

} // namespace

TEST(CartaZarrAxes, AnImageInTheProfilesOrderIsShapedAsCartaExpects) {
    std::string reason;
    const auto axes = CartaZarrAxes::FromImage(XradioOrder(), reason);
    ASSERT_TRUE(axes.has_value()) << reason;
    EXPECT_EQ(axes->Shape(), casacore::IPosition(4, 4, 5, 3, 2));
}

// Reads come back in the image's logical order, so another order would put the pixels in the wrong place
TEST(CartaZarrAxes, AnotherOrderIsNotDisplayed) {
    std::string reason;
    EXPECT_FALSE(CartaZarrAxes::FromImage(Shuffled(), reason).has_value());
    EXPECT_NE(reason.find("time, polarization, frequency, m, l"), std::string::npos) << reason;
}

TEST(CartaZarrAxes, ValuesInTheImagesOrderComeBackWithoutTime) {
    std::string reason;
    const auto axes = CartaZarrAxes::FromImage(XradioOrder(), reason);
    ASSERT_TRUE(axes.has_value()) << reason;
    // A chunk shape in the image's order: x, y, spectral, polarization, time
    EXPECT_EQ(axes->InCartaOrder({40, 50, 3, 2, 1}), (std::vector<std::uint64_t>{40, 50, 3, 2}));
    EXPECT_EQ(axes->InCartaOrder({40, 50}), (std::vector<std::uint64_t>{40, 50, 1, 1})) << "an axis not reached reads as 1";
}

TEST(CartaZarrAxes, MoreThanOneTimeIsNotDisplayed) {
    std::string reason;
    EXPECT_FALSE(CartaZarrAxes::FromImage(XradioOrder(2), reason).has_value());
    EXPECT_NE(reason.find("singleton time axis"), std::string::npos) << reason;
}

TEST(CartaZarrAxes, AnImageWithoutOneOfCartasFourAxesIsNotDisplayed) {
    std::string reason;
    const auto no_polarization =
        Described({{AxisRole::spatial_x, 4}, {AxisRole::spatial_y, 5}, {AxisRole::spectral, 3}, {AxisRole::time, 1}});
    EXPECT_FALSE(CartaZarrAxes::FromImage(no_polarization, reason).has_value());
    EXPECT_FALSE(reason.empty());
}

TEST(CartaZarrAxes, AnAxisCartaHasNoPlaceForIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    carta::zarr::AxisDescriptor extra;
    extra.name = "beam";
    extra.role = AxisRole::other;
    extra.length = 1;
    descriptor.axes.push_back(extra);
    EXPECT_FALSE(CartaZarrAxes::FromImage(descriptor, reason).has_value());
    EXPECT_NE(reason.find("beam"), std::string::npos) << "the reason should name the axis: " << reason;
}

TEST(CartaZarrAxes, AMissingCoordinateIsNotDisplayed) {
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
        EXPECT_FALSE(CartaZarrAxes::FromImage(descriptor, reason).has_value()) << "coordinate " << drop;
        EXPECT_NE(reason.find("coordinate descriptor"), std::string::npos) << reason;
    }
}

TEST(CartaZarrAxes, AnAxisLongerThanCartaCountsIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    descriptor.axes[0].length = std::uint64_t{1} << 32;
    EXPECT_FALSE(CartaZarrAxes::FromImage(descriptor, reason).has_value());
    EXPECT_NE(reason.find("too large"), std::string::npos) << reason;
}

// An empty axis is refused from the axes alone, so that the file list does not offer the image
TEST(CartaZarrAxes, AnEmptyAxisIsNotDisplayed) {
    std::string reason;
    auto descriptor = XradioOrder();
    descriptor.axes[2].length = 0;
    descriptor.spectral.reset();
    EXPECT_FALSE(CartaZarrAxes::FromListing(descriptor.axes, reason).has_value());
    EXPECT_NE(reason.find("empty axis"), std::string::npos) << reason;
}

// A spatial axis one pixel across has no pixel scale, and is refused from the axes alone
TEST(CartaZarrAxes, ASpatialAxisOfOnePixelIsNotDisplayed) {
    for (const std::size_t spatial : {0u, 1u}) {
        auto descriptor = XradioOrder();
        descriptor.axes[spatial].length = 1;
        std::string from_axes;
        std::string from_descriptor;
        EXPECT_FALSE(CartaZarrAxes::FromListing(descriptor.axes, from_axes).has_value()) << descriptor.axes[spatial].name;
        EXPECT_FALSE(CartaZarrAxes::FromImage(descriptor, from_descriptor).has_value()) << descriptor.axes[spatial].name;
        EXPECT_NE(from_axes.find("one pixel"), std::string::npos) << from_axes;
        EXPECT_NE(from_axes.find(descriptor.axes[spatial].name), std::string::npos) << from_axes;
        EXPECT_EQ(from_axes, from_descriptor);
    }
}

// Equal direction cosines leave no increment, which only the descriptor shows
TEST(CartaZarrAxes, ADirectionAxisWithNoIncrementIsNotDisplayed) {
    for (const std::size_t axis : {0u, 1u}) {
        auto descriptor = XradioOrder();
        descriptor.direction->increment[axis] = 0.0;
        std::string reason;
        EXPECT_FALSE(CartaZarrAxes::FromImage(descriptor, reason).has_value()) << axis;
        EXPECT_NE(reason.find("no increment"), std::string::npos) << reason;
    }
}

// The listing's axes and the opened image's descriptor give the same answer
TEST(CartaZarrAxes, TheAxesAloneAnswerAsTheDescriptorDoes) {
    for (const auto& descriptor : {XradioOrder(), Shuffled(), XradioOrder(2)}) {
        std::string from_axes;
        std::string from_descriptor;
        const auto listed = CartaZarrAxes::FromListing(descriptor.axes, from_axes);
        const auto opened = CartaZarrAxes::FromImage(descriptor, from_descriptor);
        ASSERT_EQ(listed.has_value(), opened.has_value()) << from_axes << " / " << from_descriptor;
        EXPECT_EQ(from_axes, from_descriptor);
        if (listed) {
            EXPECT_EQ(listed->Shape(), opened->Shape());
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
TEST(CartaZarrAxes, TheOfferedImagesAreTheOnesCartaDisplaysInTheLibrarysOrder) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {Listed("APERTURE", false, {}, "an aperture-plane image is not opened"),
        Listed("TWO_TIMES", true, XradioOrder(2).axes), Listed("SKY", true, XradioOrder().axes), Listed("MODEL", true, XradioOrder().axes)};

    const auto offer = carta::OfferedImages(dataset);
    EXPECT_EQ(offer.ids, (std::vector<std::string>{"SKY", "MODEL"}));
    ASSERT_EQ(offer.refused.size(), 2u);
    EXPECT_EQ(offer.refused[0].id, "APERTURE");
    EXPECT_EQ(offer.refused[0].reason, "an aperture-plane image is not opened");
    EXPECT_EQ(offer.refused[1].id, "TWO_TIMES");
    EXPECT_NE(offer.refused[1].reason.find("singleton time axis"), std::string::npos) << offer.refused[1].reason;
    EXPECT_TRUE(offer.why_none.empty()) << "something is offered";
}

// When nothing is offered, CARTA's reason comes first
TEST(CartaZarrAxes, NothingOfferedSaysCartasReasonBeforeTheLibrarys) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {
        Listed("APERTURE", false, {}, "an aperture-plane image is not opened"), Listed("TWO_TIMES", true, XradioOrder(2).axes)};
    const auto offer = carta::OfferedImages(dataset);
    EXPECT_TRUE(offer.ids.empty());
    EXPECT_NE(offer.why_none.find("singleton time axis"), std::string::npos) << offer.why_none;
}

// With only carta-zarr's refusals, its reason is given
TEST(CartaZarrAxes, NothingOfferedByTheLibrarySaysTheLibrarysReason) {
    carta::zarr::DatasetDescriptor dataset;
    dataset.images = {Listed("APERTURE", false, {}, "an aperture-plane image is not opened")};
    EXPECT_EQ(carta::OfferedImages(dataset).why_none, "an aperture-plane image is not opened");

    EXPECT_EQ(carta::OfferedImages(carta::zarr::DatasetDescriptor{}).why_none, "XRADIO dataset contains no image variables");
}
