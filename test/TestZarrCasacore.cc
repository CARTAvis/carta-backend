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

#include "ImageData/Zarr/ZarrCasacore.h"

using carta::MakeZarrBeamSet;
using carta::MakeZarrCoordinateSystem;
using carta::MakeZarrImageType;
using carta::MakeZarrMiscInfo;
using carta::NotesFromDiagnostics;
using carta::ZarrNoteTopic;
using carta::zarr::DiagnosticCode;

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

// The same image on the aperture plane. CASA's ia.fft writes such an image, and XRADIO converts it to Zarr.
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

// Every plane of a three-channel, two-polarization image at the first time slice, all with `major`.
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

// u and v become a linear coordinate with axes named UU and VV.
// casacore uses the same names when it takes the FFT of a direction coordinate.
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

// A spectral axis with no linear description lists the frequency of every channel
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

TEST(ZarrCasacore, TheVelocityConventionIsCarried) {
    auto descriptor = Described();
    EXPECT_EQ(MakeZarrCoordinateSystem(descriptor).coordinates.spectralCoordinate().velocityDoppler(), casacore::MDoppler::RADIO)
        << "a store that names no convention is radio";
    descriptor.spectral->doppler_type = "z";
    EXPECT_EQ(MakeZarrCoordinateSystem(descriptor).coordinates.spectralCoordinate().velocityDoppler(), casacore::MDoppler::Z);
    descriptor.spectral->doppler_type = "relativistic";
    EXPECT_EQ(MakeZarrCoordinateSystem(descriptor).coordinates.spectralCoordinate().velocityDoppler(), casacore::MDoppler::BETA);
    descriptor.spectral->doppler_type = "Optical";
    EXPECT_EQ(MakeZarrCoordinateSystem(descriptor).coordinates.spectralCoordinate().velocityDoppler(), casacore::MDoppler::RADIO)
        << "a value that the schema does not allow is radio";
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

// The pointing center of an image with an equatorial direction, in degrees
casacore::Vector<casacore::Double> PointingOf(
    const std::string& frame, double longitude, double latitude, carta::ZarrCoordinates* made = nullptr) {
    auto descriptor = Described();
    carta::zarr::ObservationInfo observation;
    observation.pointing_center = carta::zarr::SkyDirection{frame, std::nullopt, {longitude, latitude}};
    descriptor.observation = observation;
    auto coordinates = MakeZarrCoordinateSystem(descriptor);
    const auto info = coordinates.coordinates.obsInfo();
    if (made) {
        *made = coordinates;
    }
    if (info.isPointingCenterInitial()) {
        return {};
    }
    return info.pointingCenter().getAngle("deg").getValue();
}

TEST(ZarrCasacore, ThePointingCenterIsInTheFrameOfTheDirection) {
    const auto fk5 = PointingOf("FK5", 10.5, 20.25);
    ASSERT_EQ(fk5.size(), 2u);
    EXPECT_NEAR(fk5[0], 10.5, 1e-9);
    EXPECT_NEAR(fk5[1], 20.25, 1e-9);

    // The north galactic pole, which is at RA 192.85948 and Dec 27.12825 in J2000. casacore puts it 0.5" away.
    const auto pole = PointingOf("GALACTIC", 0.0, 90.0);
    ASSERT_EQ(pole.size(), 2u);
    // casacore gives the longitude between -180 and 180 degrees
    EXPECT_NEAR(std::fmod(pole[0] + 360.0, 360.0), 192.85948, 1e-3)
        << "a pointing center in another frame is converted to the image's frame";
    EXPECT_NEAR(pole[1], 27.12825, 1e-3);
}

TEST(ZarrCasacore, APointingCenterAtAnotherEquinoxIsReadAtTheFramesAndSaid) {
    auto descriptor = Described();
    carta::zarr::ObservationInfo observation;
    observation.pointing_center = carta::zarr::SkyDirection{"FK5", 1950.0, {10.5, 20.25}};
    descriptor.observation = observation;
    const auto made = MakeZarrCoordinateSystem(descriptor);
    const auto pointing = made.coordinates.obsInfo().pointingCenter().getAngle("deg").getValue();
    EXPECT_NEAR(pointing[0], 10.5, 1e-9);
    EXPECT_NEAR(pointing[1], 20.25, 1e-9);
    const auto said = std::find_if(made.notes.begin(), made.notes.end(),
        [](const carta::ZarrNote& note) { return note.detail.find("pointing center frame FK5 names equinox 1950") != std::string::npos; });
    EXPECT_NE(said, made.notes.end());
}

// AZEL needs the time and the place of the observation to be converted. Without them it is left out.
TEST(ZarrCasacore, APointingCenterInAzelUsesTheObservation) {
    auto descriptor = Described();
    carta::zarr::ObservationInfo observation;
    observation.observation_mjd = 59000.0;
    observation.observatory_position = std::array<double, 3>{2225142.18, -5440307.37, -2481029.85}; // ALMA
    observation.pointing_center = carta::zarr::SkyDirection{"AZEL", std::nullopt, {180.0, 60.0}};
    descriptor.observation = observation;
    const auto made = MakeZarrCoordinateSystem(descriptor);
    ASSERT_FALSE(made.coordinates.obsInfo().isPointingCenterInitial()) << (made.notes.empty() ? "" : made.notes.back().detail);
    // South at elevation 60 is 30 degrees south of the zenith, at ALMA's latitude of -23 degrees
    EXPECT_NEAR(made.coordinates.obsInfo().pointingCenter().getAngle("deg").getValue()[1], -53.0, 0.5);

    descriptor.observation->observation_mjd.reset();
    descriptor.observation->observatory_position.reset();
    EXPECT_TRUE(MakeZarrCoordinateSystem(descriptor).coordinates.obsInfo().isPointingCenterInitial())
        << "a conversion that needs the time and the place does not use made-up ones";

    descriptor.observation->observation_mjd = 59000.0;
    descriptor.observation->telescope_name = "ALMA";
    EXPECT_FALSE(MakeZarrCoordinateSystem(descriptor).coordinates.obsInfo().isPointingCenterInitial()) << "casacore knows where ALMA is";
}

TEST(ZarrCasacore, APointingCenterAtZeroIsNotSet) {
    EXPECT_TRUE(PointingOf("FK5", 0.0, 0.0).empty()) << "(0,0) is casacore's pointing center before it is set";
    EXPECT_FALSE(PointingOf("FK5", 0.0, 30.0).empty()) << "a pointing center with one coordinate at 0 is set";
}

TEST(ZarrCasacore, APointingCenterInAFrameCasacoreLacksIsLeftOutAndSaid) {
    carta::ZarrCoordinates made;
    EXPECT_TRUE(PointingOf("NOT_A_FRAME", 10.0, 20.0, &made).empty());
    const auto said = std::find_if(made.notes.begin(), made.notes.end(),
        [](const carta::ZarrNote& note) { return note.detail.find("NOT_A_FRAME") != std::string::npos; });
    EXPECT_NE(said, made.notes.end());
}

TEST(ZarrCasacore, AnApertureImageKeepsThePointingCenterAsWritten) {
    auto descriptor = ApertureDescribed();
    carta::zarr::ObservationInfo observation;
    observation.pointing_center = carta::zarr::SkyDirection{"GALACTIC", std::nullopt, {10.0, 20.0}};
    descriptor.observation = observation;
    const auto info = MakeZarrCoordinateSystem(descriptor).coordinates.obsInfo();
    ASSERT_FALSE(info.isPointingCenterInitial());
    const auto pointing = info.pointingCenter().getAngle("deg").getValue();
    EXPECT_NEAR(pointing[0], 10.0, 1e-9) << "an image without a direction has no frame to convert to";
    EXPECT_NEAR(pointing[1], 20.0, 1e-9);
}

TEST(ZarrCasacore, UserKeywordsAreMiscInfoOfTheirTypes) {
    const auto record =
        MakeZarrMiscInfo({{"instrume", std::string("ALMA")}, {"ncombine", std::int64_t{3}}, {"large", std::int64_t{4294967297}},
            {"huge", std::int64_t{9007199254740993}}, {"distance", 101.5}, {"pbcor", true}, {"", std::string("no name")}});
    EXPECT_EQ(record.nfields(), 6u) << "a keyword with no name is left out";
    EXPECT_EQ(record.asString("instrume"), "ALMA");
    EXPECT_EQ(record.dataType("ncombine"), casacore::TpInt);
    EXPECT_EQ(record.asInt("ncombine"), 3);
    EXPECT_EQ(record.dataType("large"), casacore::TpDouble) << "casacore writes an integer card from 32 bits";
    EXPECT_DOUBLE_EQ(record.asDouble("large"), 4294967297.0);
    EXPECT_EQ(record.asString("huge"), "9007199254740993") << "a Double would round it";
    EXPECT_DOUBLE_EQ(record.asDouble("distance"), 101.5);
    EXPECT_TRUE(record.asBool("pbcor"));
}

TEST(ZarrCasacore, AUserKeywordDoesNotReplaceACardOfTheImage) {
    const auto record = MakeZarrMiscInfo({{"bunit", std::string("K")}, {"crval4", 2.0}, {"NAXIS1", std::int64_t{5}}, {"pc1_2", 0.5},
        {"date", 5.0}, {"ORIGIN", std::int64_t{5}}, {"comment", std::string("a comment")}, {"History", std::string("made")},
        {"casambm", 1.5}, {"field", std::string("J0423-0120")}});
    EXPECT_EQ(record.nfields(), 1u);
    EXPECT_EQ(record.asString("field"), "J0423-0120");
}

TEST(ZarrCasacore, AUserKeywordThatIsNotAFitsKeywordIsLeftOut) {
    const auto record = MakeZarrMiscInfo({{"key.cdelt1", std::string("cdelt1")}, {"key.cdelt2", std::string("cdelt2")},
        {"key.x", std::string("dot")}, {"telescop1", std::string("too long")}, {"class___", std::string("kept")},
        {"proj-id", std::string("kept")}, {"OBSGEO-Z", 1.0}, {"2ndpass", true}});
    EXPECT_EQ(record.nfields(), 4u);
    EXPECT_EQ(record.asString("class___"), "kept");
    EXPECT_EQ(record.asString("proj-id"), "kept");
    EXPECT_TRUE(record.isDefined("OBSGEO-Z"));
    EXPECT_TRUE(record.isDefined("2ndpass"));
}

TEST(ZarrCasacore, AUserKeywordStringIsCutToACard) {
    const auto record = MakeZarrMiscInfo({{"note", std::string(100, 'x')}});
    EXPECT_EQ(std::string(record.asString("note")), std::string(68, 'x'));

    // "é" is two bytes, at bytes 67 and 68, so the cut would split it
    const auto accented = MakeZarrMiscInfo({{"note", std::string(67, 'x') + "\xC3\xA9" + "y"}});
    EXPECT_EQ(std::string(accented.asString("note")), std::string(67, 'x'));
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

// Beams are made in arcsec and degrees, the same units as the beams of a FITS image.
// XRADIO stores beams in radians. casacore's FITS writer writes the position angle of a BEAMS table in
// degrees, but it labels the angle with the unit the beam was made in. So a beam made in radians would be
// saved as 42.7 "rad" and read back as -73.1 degrees.
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

TEST(ZarrCasacore, OnlyTheFirstTimeSliceIsRead) {
    auto table = EveryPlane(2.0e-5);
    for (const auto& beam : EveryPlane(3.0e-5)) {
        auto later = beam;
        later.time = 1;
        table.push_back(later);
    }
    const auto made = MakeZarrBeamSet(table, 3, 2);
    ASSERT_TRUE(made.beams.has_value());
    EXPECT_EQ(made.beams->nelements(), 1u) << "a second time slice's beams are not this image's";
    EXPECT_TRUE(made.notes.empty());

    std::vector<carta::zarr::Beam> later_only;
    for (const auto& beam : EveryPlane(2.0e-5)) {
        later_only.push_back(Beam(1, beam.channel, beam.polarization, beam.major));
    }
    EXPECT_FALSE(MakeZarrBeamSet(later_only, 3, 2).beams.has_value()) << "nothing at the first time slice is no beam";
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

namespace {

carta::zarr::Diagnostic Said(DiagnosticCode code, const std::string& axis) {
    return carta::zarr::Diagnostic{code, "the whole sentence about " + axis, axis};
}

} // namespace

TEST(ZarrNotes, ACoordinateDiagnosticQualifiesTheValueItIsAbout) {
    const auto notes =
        NotesFromDiagnostics({Said(DiagnosticCode::nonuniform_axis, "frequency"), Said(DiagnosticCode::inexact_reference_pixel, "l"),
            Said(DiagnosticCode::nonuniform_axis, "m"), Said(DiagnosticCode::degenerate_axis, "frequency")});
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0].topic, ZarrNoteTopic::frequency);
    EXPECT_EQ(notes[0].brief, "channels not evenly spaced");
    EXPECT_EQ(notes[0].detail, "the whole sentence about frequency") << "the library's sentence is kept for the log";
    EXPECT_EQ(notes[1].topic, ZarrNoteTopic::reference_pixels);
    EXPECT_EQ(notes[1].brief, "l reference pixel extrapolated");
    EXPECT_EQ(notes[2].topic, ZarrNoteTopic::pixel_increment);
    EXPECT_EQ(notes[2].brief, "m samples not evenly spaced");
    EXPECT_EQ(notes[3].topic, ZarrNoteTopic::frequency);
}

// A code that is not about a value in the file info, or a code the backend does not know, has no topic.
// Its brief is the code's name, and the note only goes to the log.
TEST(ZarrNotes, ACodeWithNoValueToQualifyIsSaidByName) {
    const auto notes = NotesFromDiagnostics({Said(DiagnosticCode::unreadable_array, "SKY")});
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].topic, ZarrNoteTopic::none);
    EXPECT_EQ(notes[0].brief, "unreadable_array");
}
