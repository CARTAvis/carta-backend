/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrCasacore.h"

#include <algorithm>
#include <cstddef>
#include <sstream>

#include <casacore/casa/Arrays/Matrix.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Quanta/MVPosition.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MEpoch.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/measures/Measures/MPosition.h>
#include <casacore/measures/Measures/Stokes.h>

namespace carta {
namespace {

casacore::MDirection::Types DirectionReferenceFrame(const std::string& frame) {
    if (frame.empty() || frame == "FK5") {
        return casacore::MDirection::J2000;
    }
    if (frame == "FK4") {
        return casacore::MDirection::B1950;
    }
    if (frame == "SUPERGALACTIC") {
        return casacore::MDirection::SUPERGAL;
    }

    casacore::MDirection::Types result = casacore::MDirection::DEFAULT;
    if (!casacore::MDirection::getType(result, casacore::String(frame))) {
        throw casacore::AipsError("Unsupported XRADIO direction reference frame: " + frame);
    }
    return result;
}

casacore::MFrequency::Types SpectralReferenceFrame(const std::string& frame) {
    if (frame.empty()) {
        return casacore::MFrequency::LSRK;
    }

    casacore::MFrequency::Types result = casacore::MFrequency::DEFAULT;
    if (!casacore::MFrequency::getType(result, casacore::String(frame))) {
        throw casacore::AipsError("Unsupported XRADIO spectral reference frame: " + frame);
    }
    return result;
}

casacore::Projection DirectionProjection(const carta::zarr::DirectionCoordinate& descriptor) {
    const auto& name = descriptor.projection;
    if (name.empty()) {
        return casacore::Projection();
    }

    const auto type = casacore::Projection::type(casacore::String(name));
    if (type == casacore::Projection::N_PROJ) {
        throw casacore::AipsError("Unsupported XRADIO direction projection: " + descriptor.projection);
    }

    casacore::Vector<casacore::Double> parameters(static_cast<casacore::uInt>(descriptor.projection_parameters.size()));
    for (casacore::uInt index = 0; index < parameters.size(); ++index) {
        parameters[index] = descriptor.projection_parameters[index];
    }
    return parameters.empty() ? casacore::Projection(type) : casacore::Projection(type, parameters);
}

casacore::Vector<casacore::String> CoordinateUnits(const std::string& first, const std::string& second) {
    casacore::Vector<casacore::String> units(2);
    units[0] = first;
    units[1] = second;
    return units;
}

casacore::DirectionCoordinate MakeDirectionCoordinate(const carta::zarr::DirectionCoordinate& descriptor) {
    casacore::Matrix<casacore::Double> transform(2, 2);
    for (casacore::uInt row = 0; row < 2; ++row) {
        for (casacore::uInt column = 0; column < 2; ++column) {
            transform(row, column) = descriptor.transformation_matrix[row][column];
        }
    }

    casacore::DirectionCoordinate coordinate(DirectionReferenceFrame(descriptor.reference_frame), DirectionProjection(descriptor),
        casacore::Quantity(descriptor.reference_value[0], "deg"), casacore::Quantity(descriptor.reference_value[1], "deg"),
        casacore::Quantity(descriptor.increment[0], "deg"), casacore::Quantity(descriptor.increment[1], "deg"), transform,
        descriptor.reference_pixel[0] - 1.0, descriptor.reference_pixel[1] - 1.0,
        casacore::Quantity(descriptor.native_pole_direction[0], "deg"), casacore::Quantity(descriptor.native_pole_direction[1], "deg"));
    coordinate.setWorldAxisUnits(CoordinateUnits("deg", "deg"));
    return coordinate;
}

casacore::SpectralCoordinate MakeSpectralCoordinate(const carta::zarr::SpectralCoordinate& descriptor) {
    if (descriptor.channel_frequencies.empty()) {
        throw casacore::AipsError("XRADIO spectral coordinate has no channel values");
    }

    const std::string unit = descriptor.unit.empty() ? "Hz" : descriptor.unit;
    casacore::Vector<casacore::Double> frequencies(static_cast<casacore::uInt>(descriptor.channel_frequencies.size()));
    for (casacore::uInt index = 0; index < frequencies.size(); ++index) {
        frequencies[index] = descriptor.channel_frequencies[index];
    }
    casacore::Quantum<casacore::Vector<casacore::Double>> frequency_values(frequencies, casacore::Unit(unit));
    const auto reference_type = SpectralReferenceFrame(descriptor.system);

    casacore::SpectralCoordinate coordinate;
    if (descriptor.reference_pixel && descriptor.reference_value && descriptor.increment) {
        const casacore::Quantity reference_value(*descriptor.reference_value, unit);
        const casacore::Quantity increment(*descriptor.increment, unit);
        const casacore::Quantity rest_frequency(descriptor.rest_frequency.value_or(0.0), unit);
        coordinate =
            casacore::SpectralCoordinate(reference_type, reference_value, increment, *descriptor.reference_pixel - 1.0, rest_frequency);
    } else {
        const casacore::Quantity rest_frequency(descriptor.rest_frequency.value_or(0.0), unit);
        coordinate = casacore::SpectralCoordinate(reference_type, frequency_values, rest_frequency);
    }

    casacore::Vector<casacore::String> units(1);
    units[0] = unit;
    coordinate.setWorldAxisUnits(units);
    return coordinate;
}

casacore::StokesCoordinate MakeStokesCoordinate(const carta::zarr::PolarizationCoordinate& descriptor) {
    if (descriptor.labels.empty()) {
        throw casacore::AipsError("XRADIO polarization coordinate has no labels");
    }

    casacore::Vector<casacore::Int> stokes(static_cast<casacore::uInt>(descriptor.labels.size()));
    for (casacore::uInt index = 0; index < stokes.size(); ++index) {
        const auto type = casacore::Stokes::type(casacore::String(descriptor.labels[index]));
        if (type == casacore::Stokes::Undefined) {
            throw casacore::AipsError("Unsupported XRADIO polarization label: " + descriptor.labels[index]);
        }
        stokes[index] = static_cast<casacore::Int>(type);
    }
    return casacore::StokesCoordinate(stokes);
}

// Direction, spectral and Stokes coordinates, in CARTA's axis order
casacore::CoordinateSystem MakeCoordinateSystemOnly(const carta::zarr::ImageDescriptor& descriptor) {
    casacore::CoordinateSystem coordinate_system;
    coordinate_system.addCoordinate(MakeDirectionCoordinate(*descriptor.direction));
    coordinate_system.addCoordinate(MakeSpectralCoordinate(*descriptor.spectral));
    coordinate_system.addCoordinate(MakeStokesCoordinate(*descriptor.polarization));
    return coordinate_system;
}

void SetObservationInfo(casacore::CoordinateSystem& coordinate_system, const std::optional<carta::zarr::ObservationInfo>& descriptor,
    std::vector<ZarrNote>& notes) {
    if (!descriptor) {
        return;
    }

    casacore::ObsInfo observation;
    if (!descriptor->observer.empty()) {
        observation.setObserver(casacore::String(descriptor->observer));
    }
    if (!descriptor->telescope_name.empty()) {
        observation.setTelescope(casacore::String(descriptor->telescope_name));
    }
    if (descriptor->mjd_obs) {
        casacore::MEpoch::Types epoch_type = casacore::MEpoch::UTC;
        // A time scale casacore does not know leaves out the date rather than failing the image or assuming UTC
        if (!descriptor->timesys.empty() && !casacore::MEpoch::getType(epoch_type, casacore::String(descriptor->timesys))) {
            notes.push_back(ZarrNote{ZarrNoteTopic::none, "observation date omitted",
                "observation time scale " + descriptor->timesys + " is one casacore cannot hold; the observation date is left out"});
        } else {
            observation.setObsDate(casacore::MEpoch(casacore::Quantity(*descriptor->mjd_obs, "d"), epoch_type));
        }
    }
    if (descriptor->observatory_position) {
        const auto& position = *descriptor->observatory_position;
        // MVPosition reads three doubles as Cartesian coordinates
        observation.setTelescopePosition(
            casacore::MPosition(casacore::MVPosition(position[0], position[1], position[2]), casacore::MPosition::ITRF));
    }
    coordinate_system.setObsInfo(observation);
}

// The equinox fixed by a casacore direction frame, for J2000 and B1950
std::optional<double> EquinoxOf(casacore::MDirection::Types type) {
    switch (type) {
        case casacore::MDirection::J2000:
            return 2000.0;
        case casacore::MDirection::B1950:
            return 1950.0;
        default:
            return std::nullopt;
    }
}

} // namespace

ZarrCoordinates MakeZarrCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor) {
    ZarrCoordinates made{MakeCoordinateSystemOnly(descriptor), {}};
    SetObservationInfo(made.coordinates, descriptor.observation, made.notes);

    // FK5 and FK4 become J2000 and B1950, which casacore has only at those equinoxes. Another equinox is
    // read in that frame anyway (off by the precession in between) and noted.
    const auto type = made.coordinates.directionCoordinate().directionType();
    const auto fixed = EquinoxOf(type);
    const auto& named = descriptor.direction->equinox;
    if (fixed && named && *named != *fixed) {
        const std::string read_as = casacore::MDirection::showType(type);
        std::ostringstream equinox;
        equinox << *named;
        made.notes.push_back(ZarrNote{ZarrNoteTopic::celestial_frame, "equinox " + equinox.str() + " read as " + read_as,
            "direction frame " + descriptor.direction->reference_frame + " names equinox " + equinox.str() +
                ", which casacore cannot hold; its coordinates are read as " + read_as});
    }
    return made;
}

ZarrBeams MakeZarrBeamSet(const std::vector<carta::zarr::Beam>& table, casacore::uInt channels, casacore::uInt polarizations) {
    ZarrBeams result;
    bool has_first_time_plane = false;
    std::size_t max_channel = 0;
    std::size_t max_polarization = 0;
    for (const auto& beam : table) {
        if (beam.time == 0) {
            has_first_time_plane = true;
            max_channel = std::max(max_channel, beam.channel);
            max_polarization = std::max(max_polarization, beam.polarization);
        }
    }
    if (!has_first_time_plane) {
        return result;
    }

    // The matrix is sized from the image, since casacore rejects a beam set that does not match the axes;
    // planes missing from the table keep the null beam. It is set as a whole, because casacore only
    // computes the smallest and largest beams then.
    if (max_channel >= channels || max_polarization >= polarizations) {
        // Beams for planes the image does not have
        result.notes.push_back(ZarrNote{ZarrNoteTopic::none, "beam table ignored",
            "beam table names channel " + std::to_string(max_channel) + " polarization " + std::to_string(max_polarization) +
                " of an image with " + std::to_string(channels) + " and " + std::to_string(polarizations) + "; ignoring the beams"});
        return result;
    }
    casacore::Matrix<casacore::GaussianBeam> beam_matrix(channels, polarizations);
    std::size_t filled = 0;
    const carta::zarr::Beam* first = nullptr;
    bool every_plane_alike = true;
    for (const auto& beam : table) {
        if (beam.time != 0) {
            continue;
        }
        // casacore throws for an invalid beam; the beams are then ignored rather than failing the image
        try {
            beam_matrix(static_cast<casacore::uInt>(beam.channel), static_cast<casacore::uInt>(beam.polarization)) =
                casacore::GaussianBeam(casacore::Quantity(beam.major, beam.unit), casacore::Quantity(beam.minor, beam.unit),
                    casacore::Quantity(beam.position_angle, beam.unit));
        } catch (const casacore::AipsError& error) {
            result.notes.push_back(ZarrNote{ZarrNoteTopic::none, "beam table ignored",
                "beam table gives channel " + std::to_string(beam.channel) + " polarization " + std::to_string(beam.polarization) +
                    " a beam casacore refuses (" + std::string(error.getMesg()) + "); ignoring the beams"});
            return result;
        }
        ++filled;

        if (first == nullptr) {
            first = &beam;
        } else if (beam.major != first->major || beam.minor != first->minor || beam.position_angle != first->position_angle ||
                   beam.unit != first->unit) {
            every_plane_alike = false;
        }
    }
    const auto planes = static_cast<std::size_t>(channels) * polarizations;
    if (filled != planes) {
        const std::string covers = "covers " + std::to_string(filled) + " of " + std::to_string(planes) + " planes";
        result.notes.push_back(ZarrNote{ZarrNoteTopic::beam, "table " + covers, "beam table " + covers});
    }

    // The same beam on every plane is a single beam, so that consumers such as ImageMoments do not convolve
    // the cube to a common beam. Beams must be exactly equal.
    if (every_plane_alike && first != nullptr && filled == planes) {
        result.beams = casacore::ImageBeamSet(beam_matrix(0, 0));
    } else {
        result.beams = casacore::ImageBeamSet(beam_matrix);
    }
    return result;
}

} // namespace carta
