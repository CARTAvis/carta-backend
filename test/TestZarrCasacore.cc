/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include <cmath>

#include <algorithm>

#include <functional>
#include <string>
#include <vector>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>

#include "ImageData/ZarrCasacore.h"

using carta::MakeZarrBeamSet;
using carta::MakeZarrCoordinateSystem;
using carta::MakeZarrImageType;

namespace {

carta::zarr::ImageDescriptor Described() {
    carta::zarr::ImageDescriptor descriptor;

    carta::zarr::DirectionCoordinate direction;
    direction.projection = "SIN";
    direction.reference_value = {10.0, 20.0};
    direction.increment = {-0.001, 0.001};
    direction.reference_pixel = {3.0, 4.0}; // counted from one, as FITS counts
    direction.native_pole_direction = {0.0, 90.0};
    descriptor.direction = direction;

    carta::zarr::SpectralCoordinate spectral;
    spectral.unit = "Hz";
    spectral.reference_pixel = 1.0;
    spectral.reference_value = 1.4e9;
    spectral.increment = 1.0e6;
    spectral.rest_frequency = 1.420405751e9;
    spectral.values = {1.4e9, 1.401e9, 1.402e9};
    descriptor.spectral = spectral;

    carta::zarr::PolarizationCoordinate polarization;
    polarization.labels = {"I", "Q"};
    descriptor.polarization = polarization;
    return descriptor;
}

// The same image on the aperture plane, as CASA's ia.fft writes one and XRADIO converts it
carta::zarr::ImageDescriptor ApertureDescribed() {
    auto descriptor = Described();
    descriptor.direction.reset();
    carta::zarr::ApertureCoordinate aperture;
    aperture.unit = "lambda";
    aperture.reference_pixel = {3.0, 3.0}; // counted from one, as FITS counts
    aperture.reference_value = {0.0, 0.0};
    aperture.increment = {-11459.155902616463, 14323.94487827058};
    descriptor.aperture = aperture;
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

} // namespace

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

// FK5 at another equinox is read as J2000, with a note
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

// u and v become a linear coordinate named as casacore's own Fourier transform of a direction names them
TEST(ZarrCasacore, AnApertureImageHasUuAndVvInPlaceOfADirection) {
    const auto made = MakeZarrCoordinateSystem(ApertureDescribed());
    const auto& coordinates = made.coordinates;
    ASSERT_EQ(coordinates.nCoordinates(), 3u);
    EXPECT_EQ(coordinates.type(0), casacore::Coordinate::LINEAR);
    EXPECT_EQ(coordinates.type(1), casacore::Coordinate::SPECTRAL);
    EXPECT_EQ(coordinates.type(2), casacore::Coordinate::STOKES);
    EXPECT_FALSE(coordinates.hasDirectionCoordinate());

    const auto& aperture = coordinates.linearCoordinate(0);
    EXPECT_EQ(aperture.worldAxisNames()[0], "UU");
    EXPECT_EQ(aperture.worldAxisNames()[1], "VV");
    EXPECT_EQ(aperture.worldAxisUnits()[0], "lambda");
    EXPECT_EQ(aperture.worldAxisUnits()[1], "lambda");
    EXPECT_DOUBLE_EQ(aperture.increment()[0], -11459.155902616463);
    EXPECT_DOUBLE_EQ(aperture.increment()[1], 14323.94487827058);
    EXPECT_DOUBLE_EQ(aperture.referencePixel()[0], 2.0) << "a descriptor counts from one and casacore from zero";
    EXPECT_DOUBLE_EQ(aperture.referencePixel()[1], 2.0);
    EXPECT_TRUE(made.notes.empty()) << "the aperture plane has no celestial frame to note";
}

// Without a linear description the channels are not evenly spaced, so the axis lists them
TEST(ZarrCasacore, ASpectralAxisWithNoLinearDescriptionIsTabular) {
    auto descriptor = Described();
    descriptor.spectral->reference_pixel.reset();
    descriptor.spectral->reference_value.reset();
    descriptor.spectral->increment.reset();
    descriptor.spectral->values = {1.4e9, 1.401e9, 1.403e9};

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
    observation.observation_mjd = 59000.0;
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

// A time scale casacore does not know leaves out only the observation date
TEST(ZarrCasacore, AnObservationTimeScaleCasacoreCannotHoldCostsOnlyTheDate) {
    auto descriptor = Described();
    carta::zarr::ObservationInfo observation;
    observation.observer = "CARTA";
    observation.telescope_name = "Test scope";
    observation.observation_mjd = 59000.0;
    observation.time_scale = "NOT_A_TIME_SCALE";
    descriptor.observation = observation;

    carta::ZarrCoordinates made;
    ASSERT_NO_THROW(made = MakeZarrCoordinateSystem(descriptor));
    const auto info = made.coordinates.obsInfo();
    EXPECT_EQ(info.observer(), "CARTA") << "what could be kept was";
    EXPECT_EQ(info.telescope(), "Test scope");
    EXPECT_DOUBLE_EQ(info.obsDate().get("d").getValue(), casacore::ObsInfo().obsDate().get("d").getValue())
        << "a date in a scale casacore cannot name is left unset, not read as UTC";
    const auto said = std::find_if(made.notes.begin(), made.notes.end(),
        [](const carta::ZarrNote& note) { return note.detail.find("NOT_A_TIME_SCALE") != std::string::npos; });
    EXPECT_NE(said, made.notes.end()) << "and the reader is told why";
}

// XRADIO spells casacore's image types without their spaces
TEST(ZarrCasacore, ASubTypeIsTheImageTypeItSpells) {
    EXPECT_EQ(MakeZarrImageType("Intensity"), casacore::ImageInfo::Intensity);
    EXPECT_EQ(MakeZarrImageType("SpectralIndex"), casacore::ImageInfo::SpectralIndex);
    EXPECT_EQ(MakeZarrImageType("OpticalDepth"), casacore::ImageInfo::OpticalDepth);
    EXPECT_EQ(MakeZarrImageType("VelocityDispersion"), casacore::ImageInfo::VelocityDispersion);
}

TEST(ZarrCasacore, NoSubTypeOrOneCasacoreLacksIsIntensity) {
    EXPECT_EQ(MakeZarrImageType(""), casacore::ImageInfo::Intensity);
    EXPECT_EQ(MakeZarrImageType("Polarization Angle"), casacore::ImageInfo::Intensity);
}

TEST(ZarrCasacore, TheSameBeamOnEveryPlaneIsOneBeam) {
    const auto made = MakeZarrBeamSet(EveryPlane(2.0e-5), 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 1u) << "or a moment would convolve the cube to the beam it already has";
    EXPECT_TRUE(made.notes.empty());
}

// XRADIO stores beams in radians, and casacore's FITS writer writes a BEAMS table's position angle in
// degrees under the unit the beam was made in. A beam made in radians was saved as 42.7 "rad", which
// read back as -73.1 degrees, so beams are made in the units a FITS image's are.
TEST(ZarrCasacore, BeamsAreInArcsecondsAndDegrees) {
    auto table = EveryPlane(4.5e-6);
    table.back().position_angle = 0.745;
    const auto made = MakeZarrBeamSet(table, 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    const auto& beam = made.beams->getBeam(2, 1);
    EXPECT_EQ(std::string(beam.getMajor().getUnit()), "arcsec");
    EXPECT_EQ(std::string(beam.getMinor().getUnit()), "arcsec");
    EXPECT_EQ(std::string(beam.getPA().getUnit()), "deg");
    EXPECT_NEAR(beam.getMajor().getValue(), 4.5e-6 * 180.0 / M_PI * 3600.0, 1e-9);
    EXPECT_NEAR(beam.getPA().getValue(), 0.745 * 180.0 / M_PI, 1e-9);

    const auto single = MakeZarrBeamSet(EveryPlane(4.5e-6), 3, 2);
    ASSERT_TRUE(single.beams.has_value());
    EXPECT_EQ(std::string(single.beams->getBeam().getPA().getUnit()), "deg");
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

// Planes missing from the table get the null beam, with a note
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

// A table casacore refuses leaves the image without beams, with a note
TEST(ZarrCasacore, ATableCasacoreRefusesIsIgnored) {
    struct Case {
        const char* what;
        std::function<void(carta::zarr::Beam&)> spoil;
    };
    for (const auto& c : {Case{"negative axes", [](carta::zarr::Beam& beam) { beam.major = beam.minor = -1.0e-5; }},
             Case{"minor larger than major", [](carta::zarr::Beam& beam) { beam.minor = 2.0 * beam.major; }},
             Case{"a unit that is not an angle", [](carta::zarr::Beam& beam) { beam.unit = "Jy"; }}}) {
        auto table = EveryPlane(2.0e-5);
        c.spoil(table[2]);
        carta::ZarrBeams made;
        ASSERT_NO_THROW(made = MakeZarrBeamSet(table, 3, 2)) << c.what;
        EXPECT_FALSE(made.beams.has_value()) << c.what;
        ASSERT_EQ(made.notes.size(), 1u) << c.what;
        EXPECT_EQ(made.notes.front().topic, carta::ZarrNoteTopic::none) << c.what;
        EXPECT_NE(made.notes.front().detail.find("ignoring the beams"), std::string::npos) << made.notes.front().detail;
    }
}
