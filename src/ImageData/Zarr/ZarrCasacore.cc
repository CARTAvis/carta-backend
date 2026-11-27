/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrCasacore.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <regex>
#include <string>
#include <type_traits>
#include <variant>

#include <casacore/casa/Arrays/Matrix.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Quanta/MVPosition.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MDoppler.h>
#include <casacore/measures/Measures/MEpoch.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/measures/Measures/MPosition.h>
#include <casacore/measures/Measures/MeasFrame.h>
#include <casacore/measures/Measures/Stokes.h>
#include <spdlog/fmt/fmt.h>

namespace carta {
namespace {

// Returns the file info value that a diagnostic on `axis` is about.
ZarrNoteTopic TopicOfAxis(const std::string& axis, ZarrNoteTopic for_direction) {
    if (axis == "frequency") {
        return ZarrNoteTopic::frequency;
    }
    if (axis == "l" || axis == "m" || axis == "u" || axis == "v") {
        return for_direction;
    }
    return ZarrNoteTopic::none;
}

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

// Returns the velocity convention that `doppler_type` names.
// An empty or unknown value gives RADIO, which is also what FITS assumes.
casacore::MDoppler::Types VelocityDoppler(const std::string& doppler_type) {
    if (doppler_type == "optical" || doppler_type == "z") {
        return casacore::MDoppler::Z;
    }
    if (doppler_type == "ratio") {
        return casacore::MDoppler::RATIO;
    }
    if (doppler_type == "true" || doppler_type == "relativistic" || doppler_type == "beta") {
        return casacore::MDoppler::BETA;
    }
    if (doppler_type == "gamma") {
        return casacore::MDoppler::GAMMA;
    }
    return casacore::MDoppler::RADIO;
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

// Makes a linear coordinate for the u and v axes of the aperture plane.
// The axes are named UU and VV. casacore uses the same names when it takes the FFT of a direction coordinate.
casacore::LinearCoordinate MakeApertureCoordinate(const carta::zarr::ApertureCoordinate& descriptor) {
    const std::string unit = descriptor.unit.empty() ? "lambda" : descriptor.unit;
    casacore::Vector<casacore::String> names(2);
    names[0] = "UU";
    names[1] = "VV";
    casacore::Vector<casacore::Double> reference_value(2);
    casacore::Vector<casacore::Double> increment(2);
    casacore::Vector<casacore::Double> reference_pixel(2);
    for (casacore::uInt axis = 0; axis < 2; ++axis) {
        reference_value[axis] = descriptor.reference_value[axis];
        increment[axis] = descriptor.increment[axis];
        reference_pixel[axis] = descriptor.reference_pixel[axis] - 1.0;
    }
    casacore::Matrix<casacore::Double> transform(2, 2, 0.0);
    transform.diagonal() = 1.0;
    return casacore::LinearCoordinate(names, CoordinateUnits(unit, unit), reference_value, increment, transform, reference_pixel);
}

casacore::SpectralCoordinate MakeSpectralCoordinate(const carta::zarr::SpectralCoordinate& descriptor) {
    if (descriptor.values.empty()) {
        throw casacore::AipsError("XRADIO spectral coordinate has no channel values");
    }

    const std::string unit = descriptor.unit.empty() ? "Hz" : descriptor.unit;
    const auto reference_type = SpectralReferenceFrame(descriptor.reference_frame);
    const casacore::Quantity rest_frequency(descriptor.rest_frequency.value_or(0.0), unit);

    casacore::SpectralCoordinate coordinate;
    if (descriptor.reference_pixel && descriptor.reference_value && descriptor.increment) {
        const casacore::Quantity reference_value(*descriptor.reference_value, unit);
        const casacore::Quantity increment(*descriptor.increment, unit);
        coordinate =
            casacore::SpectralCoordinate(reference_type, reference_value, increment, *descriptor.reference_pixel - 1.0, rest_frequency);
    } else {
        casacore::Vector<casacore::Double> frequencies(static_cast<casacore::uInt>(descriptor.values.size()));
        for (casacore::uInt index = 0; index < frequencies.size(); ++index) {
            frequencies[index] = descriptor.values[index];
        }
        const casacore::Quantum<casacore::Vector<casacore::Double>> frequency_values(frequencies, casacore::Unit(unit));
        coordinate = casacore::SpectralCoordinate(reference_type, frequency_values, rest_frequency);
    }

    casacore::Vector<casacore::String> units(1);
    units[0] = unit;
    coordinate.setWorldAxisUnits(units);
    coordinate.setVelocity("km/s", VelocityDoppler(descriptor.doppler_type));
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

// Returns the pointing center in `direction_type`, the frame of the image's direction coordinate. casacore keeps
// the pointing center without a frame, and the header shows it as OBSRA and OBSDEC in the image's frame.
// Without a direction coordinate, the pointing center stays in its own frame. `frame` holds the time and
// place that a frame such as AZEL needs, when the store gives them.
std::optional<casacore::MVDirection> PointingCenter(const carta::zarr::SkyDirection& pointing,
    std::optional<casacore::MDirection::Types> direction_type, const casacore::MeasFrame& frame, std::vector<ZarrNote>& notes) {
    const casacore::MVDirection value(casacore::Quantity(pointing.value[0], "deg"), casacore::Quantity(pointing.value[1], "deg"));
    try {
        const auto pointing_type = DirectionReferenceFrame(pointing.reference_frame);
        // As for the direction coordinate, FK5 and FK4 at another equinox are read at J2000 and B1950
        const auto fixed = EquinoxOf(pointing_type);
        if (fixed && pointing.equinox && *pointing.equinox != *fixed) {
            const auto equinox = fmt::format("{:.6g}", *pointing.equinox);
            notes.push_back(ZarrNote{ZarrNoteTopic::none,
                "pointing center equinox " + equinox + " read as " + std::string(casacore::MDirection::showType(pointing_type)),
                "pointing center frame " + pointing.reference_frame + " names equinox " + equinox +
                    ", which casacore cannot hold; the pointing center is read as " +
                    std::string(casacore::MDirection::showType(pointing_type))});
        }
        if (!direction_type || *direction_type == pointing_type) {
            return value;
        }
        const casacore::MDirection::Ref from(pointing_type, frame);
        return casacore::MDirection::Convert(casacore::MDirection(value, from), casacore::MDirection::Ref(*direction_type, frame))()
            .getValue();
    } catch (const casacore::AipsError& error) {
        notes.push_back(ZarrNote{ZarrNoteTopic::none, "pointing center omitted",
            "pointing center in frame " + pointing.reference_frame + " could not be used: " + std::string(error.getMesg()) +
                "; the pointing center is left out"});
        return std::nullopt;
    }
}

void SetObservationInfo(casacore::CoordinateSystem& coordinate_system, const std::optional<carta::zarr::ObservationInfo>& descriptor,
    std::optional<casacore::MDirection::Types> direction_type, std::vector<ZarrNote>& notes) {
    if (!descriptor) {
        return;
    }

    casacore::ObsInfo observation;
    // The time and place of the observation, for a pointing center in a frame that needs them
    casacore::MeasFrame frame;
    if (!descriptor->observer.empty()) {
        observation.setObserver(casacore::String(descriptor->observer));
    }
    if (!descriptor->telescope_name.empty()) {
        observation.setTelescope(casacore::String(descriptor->telescope_name));
    }
    if (descriptor->observation_mjd) {
        casacore::MEpoch::Types epoch_type = casacore::MEpoch::UTC;
        // A time scale casacore does not know leaves out the date rather than failing the image or assuming UTC
        if (!descriptor->time_scale.empty() && !casacore::MEpoch::getType(epoch_type, casacore::String(descriptor->time_scale))) {
            notes.push_back(ZarrNote{ZarrNoteTopic::none, "observation date omitted",
                "observation time scale " + descriptor->time_scale + " is one casacore cannot hold; the observation date is left out"});
        } else {
            observation.setObsDate(casacore::MEpoch(casacore::Quantity(*descriptor->observation_mjd, "d"), epoch_type));
            frame.set(observation.obsDate());
        }
    }
    if (descriptor->observatory_position) {
        const auto& position = *descriptor->observatory_position;
        // MVPosition reads three doubles as Cartesian coordinates
        observation.setTelescopePosition(
            casacore::MPosition(casacore::MVPosition(position[0], position[1], position[2]), casacore::MPosition::ITRF));
    }
    // casacore also sets the position of a telescope that it knows by name
    if (observation.isTelescopePositionSet()) {
        frame.set(observation.telescopePosition());
    }
    // casacore's pointing center is (0,0) until it is set, and XRADIO writes that (0,0) from a CASA image.
    // So (0,0) is taken as not set, although a real pointing at (0,0) is then left out.
    const auto& pointing_center = descriptor->pointing_center;
    if (pointing_center && (pointing_center->value[0] != 0.0 || pointing_center->value[1] != 0.0)) {
        if (const auto pointing = PointingCenter(*pointing_center, direction_type, frame, notes)) {
            observation.setPointingCenter(*pointing);
        }
    }
    coordinate_system.setObsInfo(observation);
}

} // namespace

std::vector<ZarrNote> NotesFromDiagnostics(const std::vector<carta::zarr::Diagnostic>& diagnostics) {
    std::vector<ZarrNote> notes;
    for (const auto& diagnostic : diagnostics) {
        ZarrNote note;
        note.detail = diagnostic.message;
        const auto& axis = diagnostic.node_path;
        switch (diagnostic.code) {
            case carta::zarr::DiagnosticCode::nonuniform_axis:
                note.brief = axis == "frequency" ? "channels not evenly spaced" : axis + " samples not evenly spaced";
                note.topic = TopicOfAxis(axis, ZarrNoteTopic::pixel_increment);
                break;
            case carta::zarr::DiagnosticCode::inexact_reference_pixel:
                note.brief = axis + " reference pixel extrapolated";
                note.topic = TopicOfAxis(axis, ZarrNoteTopic::reference_pixels);
                break;
            case carta::zarr::DiagnosticCode::degenerate_axis:
                note.brief = axis + " has no increment";
                note.topic = TopicOfAxis(axis, ZarrNoteTopic::pixel_increment);
                break;
            default:
                // This code is not about a value that the file info shows, or this backend does not know it.
                // The note has no topic, so it is only logged.
                note.brief = carta::zarr::DiagnosticCodeName(diagnostic.code);
                break;
        }
        notes.push_back(std::move(note));
    }
    return notes;
}

ZarrCoordinates MakeZarrCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor) {
    ZarrCoordinates made;
    // The spatial, spectral and Stokes coordinates, in CARTA's axis order. The spatial coordinate is a sky
    // image's direction, or an aperture-plane image's u and v.
    if (descriptor.aperture) {
        made.coordinates.addCoordinate(MakeApertureCoordinate(*descriptor.aperture));
    } else {
        made.coordinates.addCoordinate(MakeDirectionCoordinate(*descriptor.direction));
    }
    made.coordinates.addCoordinate(MakeSpectralCoordinate(*descriptor.spectral));
    made.coordinates.addCoordinate(MakeStokesCoordinate(*descriptor.polarization));
    std::optional<casacore::MDirection::Types> direction_type;
    if (descriptor.direction) {
        direction_type = made.coordinates.directionCoordinate().directionType();
    }
    SetObservationInfo(made.coordinates, descriptor.observation, direction_type, made.notes);
    if (!descriptor.direction) {
        return made; // the aperture plane has no celestial frame
    }

    // FK5 becomes J2000 and FK4 becomes B1950. casacore has these frames only at these two equinoxes.
    // If the store names another equinox, we still use J2000 or B1950 and add a note. The positions are then
    // off by the precession between the two equinoxes.
    const auto type = made.coordinates.directionCoordinate().directionType();
    const auto fixed = EquinoxOf(type);
    const auto& named = descriptor.direction->equinox;
    if (fixed && named && *named != *fixed) {
        const std::string read_as = casacore::MDirection::showType(type);
        const auto equinox = fmt::format("{:.6g}", *named);
        made.notes.push_back(ZarrNote{ZarrNoteTopic::celestial_frame, "equinox " + equinox + " read as " + read_as,
            "direction frame " + descriptor.direction->reference_frame + " names equinox " + equinox +
                ", which casacore cannot hold; its coordinates are read as " + read_as});
    }
    return made;
}

casacore::Record MakeZarrMiscInfo(const std::vector<carta::zarr::UserKeyword>& keywords) {
    // The cards that casacore writes from the image itself. A user keyword of the same name would add a second
    // card, change the Stokes axis that CRVALn describes, or stop the header if its type differs.
    // COMMENT and HISTORY are cards with no value, which the file info does not show.
    static const std::regex written_by_casacore(
        "SIMPLE|XTENSION|BITPIX|EXTEND|PCOUNT|GCOUNT|BUNIT|BSCALE|BZERO|BLANK|END|DATE|ORIGIN|COMMENT|HISTORY|CASAMBM|NAXIS[0-9]*|"
        "(CTYPE|CRVAL|CDELT|CRPIX|CUNIT|CROTA)[0-9]+|(PC|CD|PV|PS)[0-9]+_[0-9]+",
        std::regex::icase);
    // casacore cuts a longer name to 8 characters. It writes a name with other characters anyway and logs an error
    // each time. A HIERARCH name such as key.CDELT1 would become KEY.CDEL, and key.CDELT2 would become the same name.
    static const std::regex fits_keyword("[A-Za-z0-9_-]{1,8}");
    // The longest string a FITS card holds
    constexpr std::size_t longest_string = 68;

    casacore::Record record;
    for (const auto& keyword : keywords) {
        if (!std::regex_match(keyword.name, fits_keyword) || std::regex_match(keyword.name, written_by_casacore)) {
            continue;
        }
        const casacore::String name(keyword.name);
        std::visit(
            [&](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::string>) {
                    // The cut moves back to the start of a UTF-8 character, so that no character is split
                    auto length = std::min(value.size(), longest_string);
                    while (length < value.size() && length > 0 && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) {
                        --length;
                    }
                    record.define(name, casacore::String(value.substr(0, length)));
                } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                    // casacore writes an integer card from an Int. A larger integer is a Double while a Double holds it
                    // exactly, up to 2^53, and a string of its digits beyond.
                    constexpr std::int64_t exact_in_double = std::int64_t{1} << 53;
                    if (value >= std::numeric_limits<casacore::Int>::min() && value <= std::numeric_limits<casacore::Int>::max()) {
                        record.define(name, static_cast<casacore::Int>(value));
                    } else if (value >= -exact_in_double && value <= exact_in_double) {
                        record.define(name, static_cast<casacore::Double>(value));
                    } else {
                        record.define(name, casacore::String(std::to_string(value)));
                    }
                } else if constexpr (std::is_same_v<Value, bool>) {
                    record.define(name, casacore::Bool(value));
                } else {
                    record.define(name, casacore::Double(value));
                }
            },
            keyword.value);
    }
    return record;
}

casacore::ImageInfo::ImageTypes MakeZarrImageType(const std::string& sub_type) {
    const auto spelled = [](std::string name) {
        name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
        return casacore::upcase(casacore::String(name));
    };
    for (int index = 0; index < casacore::ImageInfo::nTypes; ++index) {
        const auto type = static_cast<casacore::ImageInfo::ImageTypes>(index);
        if (spelled(casacore::ImageInfo::imageType(type)) == spelled(sub_type)) {
            return type;
        }
    }
    return casacore::ImageInfo::defaultImageType();
}

ZarrBeams MakeZarrBeamSet(const std::vector<carta::zarr::Beam>& table, casacore::uInt channels, casacore::uInt polarizations) {
    ZarrBeams result;
    // The matrix size comes from the image, because casacore rejects a beam set that does not match the axes.
    // A plane that is missing from the table keeps the null beam.
    // We set the whole matrix at once, because casacore computes the smallest and largest beams only then.
    casacore::Matrix<casacore::GaussianBeam> beam_matrix(channels, polarizations);
    std::size_t filled = 0;
    const carta::zarr::Beam* first = nullptr;
    bool every_plane_alike = true;
    for (const auto& beam : table) {
        if (beam.time != 0) {
            continue;
        }
        if (beam.channel >= channels || beam.polarization >= polarizations) {
            // A beam for a plane the image does not have
            result.notes.push_back(ZarrNote{ZarrNoteTopic::none, "beam table ignored",
                "beam table names channel " + std::to_string(beam.channel) + " polarization " + std::to_string(beam.polarization) +
                    " of an image with " + std::to_string(channels) + " and " + std::to_string(polarizations) + "; ignoring the beams"});
            return result;
        }
        // casacore throws for an invalid beam. In that case we ignore all beams, and the image still opens.
        // We make each beam in arcsec and degrees, the same units as the beams of a FITS image.
        // The reason is casacore's FITS writer. It writes the position angle of a BEAMS table in degrees,
        // but it labels the angle with the unit the beam was made in. So a beam made in radians is saved wrongly.
        try {
            beam_matrix(static_cast<casacore::uInt>(beam.channel), static_cast<casacore::uInt>(beam.polarization)) =
                casacore::GaussianBeam(casacore::Quantity(beam.major, beam.unit).get("arcsec"),
                    casacore::Quantity(beam.minor, beam.unit).get("arcsec"), casacore::Quantity(beam.position_angle, beam.unit).get("deg"));
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
    if (first == nullptr) {
        return result; // no beam at the first time slice
    }
    const auto planes = static_cast<std::size_t>(channels) * polarizations;
    if (filled != planes) {
        const std::string covers = "covers " + std::to_string(filled) + " of " + std::to_string(planes) + " planes";
        result.notes.push_back(ZarrNote{ZarrNoteTopic::beam, "table " + covers, "beam table " + covers});
    }

    // The same beam on every plane is a single beam, so that consumers such as ImageMoments do not convolve
    // the cube to a common beam. Beams must be exactly equal.
    if (every_plane_alike && filled == planes) {
        result.beams = casacore::ImageBeamSet(beam_matrix(0, 0));
    } else {
        result.beams = casacore::ImageBeamSet(beam_matrix);
    }
    return result;
}

} // namespace carta
