/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include "ZarrContext.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include <casacore/casa/Arrays/Matrix.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/coordinates/Coordinates/Projection.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/measures/Measures/MEpoch.h>
#include <casacore/measures/Measures/MPosition.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/measures/Measures/Stokes.h>

#include <spdlog/spdlog.h>

namespace carta {
namespace {

std::vector<std::uint64_t> CartaShape(const carta::zarr::ImageDescriptor& descriptor) {
    std::vector<std::uint64_t> shape(4, 0);
    for (const auto& axis : descriptor.axes) {
        if (axis.role == carta::zarr::AxisRole::time) {
            if (axis.length != 1) {
                throw casacore::AipsError("CARTA backend currently supports only XRADIO images with a singleton time axis");
            }
        }
        switch (axis.role) {
            case carta::zarr::AxisRole::spatial_x:
                shape[0] = axis.length;
                break;
            case carta::zarr::AxisRole::spatial_y:
                shape[1] = axis.length;
                break;
            case carta::zarr::AxisRole::spectral:
                shape[2] = axis.length;
                break;
            case carta::zarr::AxisRole::polarization:
                shape[3] = axis.length;
                break;
            default:
                break;
        }
    }
    return shape;
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

    casacore::DirectionCoordinate coordinate(
        DirectionReferenceFrame(descriptor.reference_frame), DirectionProjection(descriptor),
        casacore::Quantity(descriptor.reference_value[0], "deg"), casacore::Quantity(descriptor.reference_value[1], "deg"),
        casacore::Quantity(descriptor.increment[0], "deg"), casacore::Quantity(descriptor.increment[1], "deg"), transform,
        descriptor.reference_pixel[0] - 1.0, descriptor.reference_pixel[1] - 1.0,
        casacore::Quantity(descriptor.native_pole_direction[0], "deg"),
        casacore::Quantity(descriptor.native_pole_direction[1], "deg"));
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
        coordinate = casacore::SpectralCoordinate(reference_type, reference_value, increment,
                                                  *descriptor.reference_pixel - 1.0, rest_frequency);
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

casacore::CoordinateSystem MakeCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor) {
    if (!descriptor.direction || !descriptor.spectral || !descriptor.polarization) {
        throw casacore::AipsError("XRADIO image is missing a required coordinate descriptor");
    }

    casacore::CoordinateSystem coordinate_system;
    // Keep the backend's canonical pixel order: spatial X/Y, spectral, polarization.
    coordinate_system.addCoordinate(MakeDirectionCoordinate(*descriptor.direction));
    coordinate_system.addCoordinate(MakeSpectralCoordinate(*descriptor.spectral));
    coordinate_system.addCoordinate(MakeStokesCoordinate(*descriptor.polarization));
    return coordinate_system;
}

void SetObservationInfo(casacore::CoordinateSystem& coordinate_system,
                        const std::optional<carta::zarr::ObservationInfo>& descriptor) {
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
        if (!descriptor->timesys.empty() &&
            !casacore::MEpoch::getType(epoch_type, casacore::String(descriptor->timesys))) {
            throw casacore::AipsError("Unsupported XRADIO time scale: " + descriptor->timesys);
        }
        observation.setObsDate(casacore::MEpoch(casacore::Quantity(*descriptor->mjd_obs, "d"), epoch_type));
    }
    if (descriptor->observatory_position) {
        const auto& position = *descriptor->observatory_position;
        observation.setTelescopePosition(casacore::MPosition(
            casacore::Quantity(position[0], "m"), casacore::Quantity(position[1], "m"),
            casacore::Quantity(position[2], "m"), casacore::MPosition::ITRF));
    }
    coordinate_system.setObsInfo(observation);
}

}  // namespace

CartaZarrImage::CartaZarrImage(const std::string& filename, const std::string& image_id) : _filename(filename) {
    const auto context = GetZarrContext();
    auto dataset = carta::zarr::Dataset::Open(*context, filename);
    if (!dataset) {
        throw casacore::AipsError("Failed to open XRADIO dataset: " + dataset.error().message);
    }

    if (image_id.empty()) {
        const auto& image_ids = dataset.value().descriptor().image_ids;
        if (!image_ids.empty()) {
            _image_id = image_ids.front();
        }
    } else {
        _image_id = image_id;
    }
    if (_image_id.empty()) {
        throw casacore::AipsError("XRADIO dataset contains no image variables");
    }

    auto image = dataset.value().OpenImage(_image_id);
    if (!image) {
        throw casacore::AipsError("Failed to open XRADIO image '" + _image_id + "': " + image.error().message);
    }
    _zarr_image = std::move(image.value());
    _descriptor = _zarr_image->descriptor();

    const auto shape = CartaShape(_descriptor);
    _shape = casacore::IPosition(static_cast<casacore::uInt>(shape.size()));
    for (casacore::uInt index = 0; index < _shape.size(); ++index) {
        if (shape[index] > static_cast<std::uint64_t>(std::numeric_limits<casacore::Int>::max())) {
            throw casacore::AipsError("XRADIO image dimension is too large for casacore");
        }
        _shape[index] = static_cast<casacore::Int>(shape[index]);
    }

    SetUpImage();
}

CartaZarrImage::CartaZarrImage(const CartaZarrImage& other)
    : casacore::ImageInterface<float>(other),
      _filename(other._filename),
      _image_id(other._image_id),
      _zarr_image(other._zarr_image),
      _descriptor(other._descriptor),
      _shape(other._shape) {}

CartaZarrImage::~CartaZarrImage() = default;

casacore::String CartaZarrImage::imageType() const {
    return "CartaZarrImage";
}

casacore::String CartaZarrImage::name(bool stripPath) const {
    return stripPath ? casacore::Path(_filename).baseName() : _filename;
}

casacore::IPosition CartaZarrImage::shape() const {
    return _shape;
}

casacore::Bool CartaZarrImage::ok() const {
    return _zarr_image.has_value() && _shape.size() == 4 && coordinates().nPixelAxes() == _shape.size();
}

casacore::DataType CartaZarrImage::dataType() const {
    // ImageInterface<float> is the backend's image contract. The descriptor still preserves
    // the stored type for consumers of carta-zarr itself.
    return casacore::TpFloat;
}

casacore::DataType CartaZarrImage::InternalDataType() const {
    switch (_descriptor.stored_type) {
        case carta::zarr::DataType::boolean:
            return casacore::TpBool;
        case carta::zarr::DataType::int8:
            return casacore::TpChar;
        case carta::zarr::DataType::uint8:
            return casacore::TpUChar;
        case carta::zarr::DataType::int16:
            return casacore::TpShort;
        case carta::zarr::DataType::uint16:
            return casacore::TpUShort;
        case carta::zarr::DataType::int32:
            return casacore::TpInt;
        case carta::zarr::DataType::uint32:
            return casacore::TpUInt;
        case carta::zarr::DataType::int64:
            return casacore::TpInt64;
        case carta::zarr::DataType::float16:
        case carta::zarr::DataType::float32:
            return casacore::TpFloat;
        case carta::zarr::DataType::float64:
            return casacore::TpDouble;
        default:
            return casacore::TpOther;
    }
}

casacore::Bool CartaZarrImage::doGetSlice(casacore::Array<float>& /*buffer*/, const casacore::Slicer& /*section*/) {
    throw casacore::AipsError("CartaZarrImage::doGetSlice - carta-zarr pixel reads are not implemented yet");
}

void CartaZarrImage::doPutSlice(const casacore::Array<float>& /*buffer*/, const casacore::IPosition& /*where*/, const casacore::IPosition& /*stride*/) {
    throw casacore::AipsError("CartaZarrImage::doPutSlice - image is not writable");
}

const casacore::LatticeRegion* CartaZarrImage::getRegionPtr() const {
    return nullptr;
}

void CartaZarrImage::resize(const casacore::TiledShape& /*newShape*/) {
    throw casacore::AipsError("CartaZarrImage::resize - image is not writable");
}

casacore::ImageInterface<float>* CartaZarrImage::cloneII() const {
    return new CartaZarrImage(*this);
}

casacore::Bool CartaZarrImage::isMasked() const {
    return false;
}

casacore::Bool CartaZarrImage::hasPixelMask() const {
    return false;
}

const casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() const {
    throw casacore::AipsError("CartaZarrImage::pixelMask - pixel mask reads are not implemented yet");
}

casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() {
    throw casacore::AipsError("CartaZarrImage::pixelMask - pixel mask reads are not implemented yet");
}

casacore::Bool CartaZarrImage::doGetMaskSlice(casacore::Array<casacore::Bool>& /*buffer*/, const casacore::Slicer& /*section*/) {
    throw casacore::AipsError("CartaZarrImage::doGetMaskSlice - pixel mask reads are not implemented yet");
}

std::vector<std::pair<std::string, std::string>> CartaZarrImage::GetStorageInfo() const {
    if (!_descriptor.storage) {
        return {};
    }

    const auto& storage = *_descriptor.storage;
    const auto ordered_shape = [&](const std::vector<std::uint64_t>& values) {
        std::vector<std::uint64_t> result(4, 1);
        for (const auto& axis : _descriptor.axes) {
            const std::uint64_t value = axis.storage_index < values.size() ? values[axis.storage_index] : 1;
            switch (axis.role) {
                case carta::zarr::AxisRole::spatial_x:
                    result[0] = value;
                    break;
                case carta::zarr::AxisRole::spatial_y:
                    result[1] = value;
                    break;
                case carta::zarr::AxisRole::spectral:
                    result[2] = value;
                    break;
                case carta::zarr::AxisRole::polarization:
                    result[3] = value;
                    break;
                default:
                    break;
            }
        }
        return result;
    };
    const auto format_shape = [](const std::vector<std::uint64_t>& values) {
        std::string result = "[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += std::to_string(values[index]);
        }
        return result + "] (SPATIAL_X, SPATIAL_Y, FREQ, STOKES)";
    };

    std::vector<std::pair<std::string, std::string>> entries;
    if (storage.sharded) {
        entries.emplace_back("Shard shape", format_shape(ordered_shape(storage.shard_shape)));
    }
    entries.emplace_back("Chunk shape", format_shape(ordered_shape(storage.chunk_shape)));
    if (!storage.compressor.empty()) {
        entries.emplace_back("Compressor", storage.compressor);
    }
    return entries;
}

void CartaZarrImage::SetUpImage() {
    try {
        auto coordinate_system = MakeCoordinateSystem(_descriptor);
        SetObservationInfo(coordinate_system, _descriptor.observation);
        setCoordinateInfo(coordinate_system);
        if (!_descriptor.unit.empty()) {
            setUnits(casacore::Unit(_descriptor.unit));
        }

        casacore::ImageInfo image_info;
        image_info.setImageType(casacore::ImageInfo::Intensity);
        if (_descriptor.observation && !_descriptor.observation->object_name.empty()) {
            image_info.setObjectName(casacore::String(_descriptor.observation->object_name));
        }
        setImageInfo(image_info);
        SetBeams();
    } catch (const casacore::AipsError&) {
        throw;
    } catch (const std::exception& error) {
        spdlog::error("Error opening XRADIO image {}: {}", _filename, error.what());
        throw casacore::AipsError("Error opening XRADIO image: " + std::string(error.what()));
    }
}

void CartaZarrImage::SetBeams() {
    if (!_zarr_image) {
        return;
    }
    auto beams = _zarr_image->ReadBeams();
    if (!beams) {
        spdlog::warn("Failed to read XRADIO image beams from {}: {}", _filename, beams.error().message);
        return;
    }
    if (beams.value().empty()) {
        return;
    }

    const auto& values = beams.value();
    bool has_first_time_plane = false;
    std::size_t max_channel = 0;
    std::size_t max_polarization = 0;
    for (const auto& beam : values) {
        if (beam.time == 0) {
            has_first_time_plane = true;
            max_channel = std::max(max_channel, beam.channel);
            max_polarization = std::max(max_polarization, beam.polarization);
        }
    }
    if (!has_first_time_plane) {
        return;
    }

    casacore::ImageBeamSet beam_set(static_cast<casacore::uInt>(max_channel + 1),
        static_cast<casacore::uInt>(max_polarization + 1));
    for (const auto& beam : values) {
        if (beam.time != 0) {
            continue;
        }
        beam_set.setBeam(static_cast<casacore::Int>(beam.channel), static_cast<casacore::Int>(beam.polarization),
            casacore::GaussianBeam(casacore::Quantity(beam.major, beam.unit), casacore::Quantity(beam.minor, beam.unit),
                casacore::Quantity(beam.position_angle, beam.unit)));
    }
    auto image_info = imageInfo();
    image_info.setBeams(beam_set);
    setImageInfo(image_info);
}

}  // namespace carta
