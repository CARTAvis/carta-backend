/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>

#include "ImageData/ZarrCasacore.h"

using carta::MakeZarrBeamSet;
using carta::MakeZarrCoordinateSystem;

namespace {

// Written by hand, because what is under test is what casacore is told, and no store has to exist
// to say it.
carta::zarr::ImageDescriptor Described() {
    carta::zarr::ImageDescriptor descriptor;

    carta::zarr::DirectionCoordinate direction;
    direction.projection = "SIN";
    direction.reference_value = {10.0, 20.0};
    direction.increment = {-0.001, 0.001};
    direction.reference_pixel = {3.0, 4.0};  // counted from one, as FITS counts
    direction.native_pole_direction = {0.0, 90.0};
    descriptor.direction = direction;

    carta::zarr::SpectralCoordinate spectral;
    spectral.unit = "Hz";
    spectral.reference_pixel = 1.0;
    spectral.reference_value = 1.4e9;
    spectral.increment = 1.0e6;
    spectral.rest_frequency = 1.420405751e9;
    spectral.channel_frequencies = {1.4e9, 1.401e9, 1.402e9};
    descriptor.spectral = spectral;

    carta::zarr::PolarizationCoordinate polarization;
    polarization.labels = {"I", "Q"};
    descriptor.polarization = polarization;
    return descriptor;
}

carta::zarr::Beam Beam(std::size_t time, std::size_t channel, std::size_t polarization, double major) {
    carta::zarr::Beam beam;
    beam.time = time;
    beam.channel = channel;
    beam.polarization = polarization;
    beam.major = major;
    beam.minor = major / 2.0;
    beam.position_angle = 0.1;
    beam.unit = "rad";
    return beam;
}

// Every plane of a three-channel, two-polarization image at the first time, all with `major`.
std::vector<carta::zarr::Beam> EveryPlane(double major) {
    std::vector<carta::zarr::Beam> table;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        for (std::size_t polarization = 0; polarization < 2; ++polarization) {
            table.push_back(Beam(0, channel, polarization, major));
        }
    }
    return table;
}

}  // namespace

TEST(ZarrCasacore, ReferencePixelsAreCountedFromZero) {
    const auto coordinates = MakeZarrCoordinateSystem(Described()).coordinates;
    const auto direction = coordinates.directionCoordinate().referencePixel();
    EXPECT_DOUBLE_EQ(direction[0], 2.0) << "a descriptor counts from one and casacore from zero";
    EXPECT_DOUBLE_EQ(direction[1], 3.0);
    EXPECT_DOUBLE_EQ(coordinates.spectralCoordinate().referencePixel()[0], 0.0);
}

TEST(ZarrCasacore, AnUnnamedFrameIsJ2000AndLsrk) {
    const auto coordinates = MakeZarrCoordinateSystem(Described()).coordinates;
    EXPECT_EQ(coordinates.directionCoordinate().directionType(), casacore::MDirection::J2000);
    EXPECT_EQ(coordinates.spectralCoordinate().frequencySystem(), casacore::MFrequency::LSRK);
}

TEST(ZarrCasacore, Fk4IsB1950) {
    auto descriptor = Described();
    descriptor.direction->reference_frame = "FK4";
    EXPECT_EQ(MakeZarrCoordinateSystem(descriptor).coordinates.directionCoordinate().directionType(), casacore::MDirection::B1950);
}

// casacore's J2000 is FK5 at the equinox of 2000, and there is no other FK5 to ask it for. An image
// that names another equinox is read as J2000 all the same -- close, and still worth opening -- but
// the offset is said rather than left for someone to find.
TEST(ZarrCasacore, AnEquinoxTheFrameCannotHoldIsSaid) {
    auto described = Described();
    described.direction->reference_frame = "FK5";
    described.direction->equinox = 2000.0;
    const auto at_2000 = MakeZarrCoordinateSystem(described);
    EXPECT_EQ(at_2000.coordinates.directionCoordinate().directionType(), casacore::MDirection::J2000);
    EXPECT_TRUE(at_2000.notes.empty()) << "FK5 at 2000 is exactly J2000";

    described.direction->equinox = 1975.0;
    const auto at_1975 = MakeZarrCoordinateSystem(described);
    EXPECT_EQ(at_1975.coordinates.directionCoordinate().directionType(), casacore::MDirection::J2000)
        << "still opened, as the nearest frame casacore has";
    ASSERT_EQ(at_1975.notes.size(), 1u);
    EXPECT_EQ(at_1975.notes.front().topic, carta::ZarrNoteTopic::celestial_frame) << "it qualifies the frame shown";
    EXPECT_EQ(at_1975.notes.front().brief, "equinox 1975 read as J2000");

    described.direction->reference_frame = "FK4";
    described.direction->equinox = 1950.0;
    EXPECT_TRUE(MakeZarrCoordinateSystem(described).notes.empty()) << "FK4 at 1950 is exactly B1950";

    described.direction->reference_frame = "ICRS";
    described.direction->equinox = 1975.0;
    EXPECT_TRUE(MakeZarrCoordinateSystem(described).notes.empty()) << "ICRS has no equinox to disagree with";
}

TEST(ZarrCasacore, TheCoordinatesAreInCartasOrder) {
    const auto coordinates = MakeZarrCoordinateSystem(Described()).coordinates;
    ASSERT_EQ(coordinates.nCoordinates(), 3u);
    EXPECT_EQ(coordinates.type(0), casacore::Coordinate::DIRECTION);
    EXPECT_EQ(coordinates.type(1), casacore::Coordinate::SPECTRAL);
    EXPECT_EQ(coordinates.type(2), casacore::Coordinate::STOKES);
}

// No linear description means the channels are not evenly spaced, and then the only honest axis is
// the one that lists them.
TEST(ZarrCasacore, ASpectralAxisWithNoLinearDescriptionIsTabular) {
    auto descriptor = Described();
    descriptor.spectral->reference_pixel.reset();
    descriptor.spectral->reference_value.reset();
    descriptor.spectral->increment.reset();
    descriptor.spectral->channel_frequencies = {1.4e9, 1.401e9, 1.403e9};

    const auto coordinates = MakeZarrCoordinateSystem(descriptor).coordinates;
    casacore::Double world = 0.0;
    ASSERT_TRUE(coordinates.spectralCoordinate().toWorld(world, 2.0));
    EXPECT_DOUBLE_EQ(world, 1.403e9) << "the last channel is where it was listed, not where a line would put it";
}

TEST(ZarrCasacore, WhatCasacoreCannotExpressIsRefused) {
    auto projection = Described();
    projection.direction->projection = "NOT_A_PROJECTION";
    EXPECT_THROW(MakeZarrCoordinateSystem(projection), casacore::AipsError);

    auto label = Described();
    label.polarization->labels = {"I", "NOT_A_STOKES"};
    EXPECT_THROW(MakeZarrCoordinateSystem(label), casacore::AipsError);
}

TEST(ZarrCasacore, TheObservationIsCarried) {
    auto descriptor = Described();
    carta::zarr::ObservationInfo observation;
    observation.observer = "CARTA";
    observation.telescope_name = "Test scope";
    observation.mjd_obs = 59000.0;
    observation.observatory_position = std::array<double, 3>{1.0, 2.0, 3.0};
    descriptor.observation = observation;

    const auto info = MakeZarrCoordinateSystem(descriptor).coordinates.obsInfo();
    EXPECT_EQ(info.observer(), "CARTA");
    EXPECT_EQ(info.telescope(), "Test scope");
    EXPECT_DOUBLE_EQ(info.obsDate().get("d").getValue(), 59000.0);
    const auto position = info.telescopePosition().getValue().getValue();
    EXPECT_DOUBLE_EQ(position[0], 1.0) << "three doubles are Cartesian, not longitude, latitude and height";
    EXPECT_DOUBLE_EQ(position[2], 3.0);
}

TEST(ZarrCasacore, TheSameBeamOnEveryPlaneIsOneBeam) {
    const auto made = MakeZarrBeamSet(EveryPlane(2.0e-5), 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 1u) << "or a moment would convolve the cube to the beam it already has";
    EXPECT_TRUE(made.notes.empty());
}

TEST(ZarrCasacore, BeamsThatDifferStayMany) {
    auto table = EveryPlane(2.0e-5);
    table.back().major = 3.0e-5;
    const auto made = MakeZarrBeamSet(table, 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 6u);
}

TEST(ZarrCasacore, OnlyTheFirstTimeIsRead) {
    auto table = EveryPlane(2.0e-5);
    for (const auto& beam : EveryPlane(3.0e-5)) {
        auto later = beam;
        later.time = 1;
        table.push_back(later);
    }
    const auto made = MakeZarrBeamSet(table, 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 1u) << "a second time's beams are not this image's";
    EXPECT_TRUE(made.notes.empty());

    std::vector<carta::zarr::Beam> later_only;
    for (const auto& beam : EveryPlane(2.0e-5)) {
        later_only.push_back(Beam(1, beam.channel, beam.polarization, beam.major));
    }
    EXPECT_FALSE(MakeZarrBeamSet(later_only, 3, 2).beams.has_value()) << "nothing at the first time is no beam";
}

// A table that stops short leaves the missing planes the null beam, which opens, rather than a set
// casacore would refuse -- and says so.
TEST(ZarrCasacore, ATableThatCoversSomePlanesSaysSo) {
    auto table = EveryPlane(2.0e-5);
    table.pop_back();
    const auto made = MakeZarrBeamSet(table, 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 6u) << "one plane has the null beam, so they are not all alike";
    ASSERT_EQ(made.notes.size(), 1u);
    EXPECT_EQ(made.notes.front().topic, carta::ZarrNoteTopic::beam);
    EXPECT_EQ(made.notes.front().brief, "table covers 5 of 6 planes");
}

TEST(ZarrCasacore, ATableNamingPlanesTheImageLacksIsIgnored) {
    auto table = EveryPlane(2.0e-5);
    table.push_back(Beam(0, 3, 0, 2.0e-5));
    const auto made = MakeZarrBeamSet(table, 3, 2);
    EXPECT_FALSE(made.beams.has_value());
    ASSERT_EQ(made.notes.size(), 1u);
    EXPECT_EQ(made.notes.front().topic, carta::ZarrNoteTopic::none) << "with no beams there is no beam entry to sit beside";
    EXPECT_NE(made.notes.front().detail.find("ignoring the beams"), std::string::npos) << made.notes.front().detail;
}
