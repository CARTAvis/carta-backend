/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include "ZarrContext.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
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
#include <casacore/casa/Quanta/MVPosition.h>
#include <casacore/measures/Measures/MPosition.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/measures/Measures/Stokes.h>

#include <spdlog/spdlog.h>

namespace carta {
namespace {

// Sections this size or smaller keep their mask whatever shape they are: a byte per pixel of four
// mebipixels is cheap enough not to reason about.
constexpr casacore::Int kMaskCacheMaxPixels = 1 << 22;

// Above that, only a section spanning more than one plane keeps its mask, and only up to this many
// pixels.
//
// The raster path reads one plane at a time and never asks for a mask, so computing one there
// costs a byte per pixel and an isFinite pass per channel change for nothing -- that is what the
// small cap above was protecting. A section covering many channels at once is not the raster path;
// it is a casacore cube walk, and those ask for the mask immediately after the pixels, for the
// same section.
//
// Getting this wrong is expensive rather than merely wasteful, because the fallback in
// doGetMaskSlice is to read the section again: a moment over a 512x512x7776 cube walks it in
// 512x107x7776 slabs, 426 mebipixels each, and missing here made it decode the whole cube twice.
constexpr casacore::Int kMaskCacheMaxCubePixels = 1 << 30;

// What the backend makes of a library call that did not succeed, said once for every call this
// image passes through. A cancellation is the caller's own decision arriving back -- a sink or a
// progress callback that said stop, or a cancellation_requested that said yes -- so it is false
// rather than an error to report upwards. Anything else is thrown, which is how a casacore image
// reports failure.
template <typename T>
bool Finished(const carta::zarr::Result<T>& result, const char* where) {
    if (result) {
        return true;
    }
    if (result.error().code == carta::zarr::ErrorCode::cancelled) {
        return false;
    }
    throw casacore::AipsError(std::string("CartaZarrImage::") + where + " - " + result.error().message);
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

// Only ever asked of an image CartaZarrAxes accepted, which is what says the three are there.
casacore::CoordinateSystem MakeCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor) {
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
        // MVPosition interprets three doubles as Cartesian coordinates.
        observation.setTelescopePosition(casacore::MPosition(
            casacore::MVPosition(position[0], position[1], position[2]), casacore::MPosition::ITRF));
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
        const auto& default_image_id = dataset.value().descriptor().default_image_id;
        if (default_image_id) {
            _image_id = *default_image_id;
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

    std::string reason;
    _axes = CartaZarrAxes::Of(_descriptor, reason);
    if (!_axes) {
        throw casacore::AipsError(reason);
    }
    _shape = _axes->Shape();

    SetUpImage();
}

CartaZarrImage::CartaZarrImage(const CartaZarrImage& other)
    : casacore::ImageInterface<float>(other),
      _filename(other._filename),
      _image_id(other._image_id),
      _zarr_image(other._zarr_image),
      _axes(other._axes),
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

// casacore's default fills axis 0, which is l - the axis that is not contiguous on disk. On a
// 7763-wide image that makes every cursor one row spanning about thirty chunks, so a plane's
// statistics decode the whole plane once per row. Report the chunk instead, the way
// CartaFitsImage reports its tile shape.
//
// max_pixels is advice, and it is followed only as far as it helps: a cursor smaller than one
// chunk decodes exactly as much as a whole chunk does, so the non-spatial axes are collapsed to
// fit and the spatial chunk itself is reported even when it is larger than the advice.
casacore::IPosition CartaZarrImage::doNiceCursorShape(casacore::uInt max_pixels) const {
    if (!_zarr_image) {
        return casacore::ImageInterface<float>::doNiceCursorShape(max_pixels);
    }
    const auto& own_chunk = _zarr_image->chunk_geometry().chunk_shape;
    if (own_chunk.size() < _descriptor.axes.size()) {
        return casacore::ImageInterface<float>::doNiceCursorShape(max_pixels);
    }
    const auto chunk = _axes->InCartaOrder(own_chunk);

    casacore::IPosition cursor(_shape.size());
    for (casacore::uInt axis = 0; axis < _shape.size(); ++axis) {
        cursor(axis) = std::min<casacore::Int>(static_cast<casacore::Int>(chunk[axis]), _shape(axis));
    }

    // Collapse the non-spatial axes, slowest first, until the advice is met.
    for (casacore::uInt axis = _shape.size(); axis > 2 && cursor.product() > static_cast<casacore::Int>(max_pixels);
         --axis) {
        cursor(axis - 1) = 1;
    }

    // Grow by whole chunks while the advice still allows it: one chunk per cursor is one small
    // request per chunk, which leaves the decode pool idle.
    for (casacore::uInt axis = 0; axis < 2; ++axis) {
        const auto step = static_cast<casacore::Int>(chunk[axis]);
        while (cursor(axis) + step <= _shape(axis) &&
               (cursor.product() / cursor(axis)) * (cursor(axis) + step) <= static_cast<casacore::Int>(max_pixels)) {
            cursor(axis) += step;
        }
    }
    return cursor;
}

casacore::Bool CartaZarrImage::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    Read(buffer, section);

    const auto length = section.length();
    const bool one_plane = length.size() < 3 || (length(2) <= 1 && (length.size() < 4 || length(3) <= 1));
    const casacore::Int most_pixels = one_plane ? kMaskCacheMaxPixels : kMaskCacheMaxCubePixels;
    if (section.length().product() <= most_pixels) {
        casacore::Array<casacore::Bool> mask = isFinite(buffer);
        std::scoped_lock lock(_mask_cache_mutex);
        _mask_cache_start = section.start();
        _mask_cache_length = section.length();
        _mask_cache_stride = section.stride();
        _mask_cache.reference(mask);
    }
    return false;
}

const carta::zarr::Image& CartaZarrImage::Opened(const char* where) const {
    if (!_zarr_image) {
        throw casacore::AipsError(std::string("CartaZarrImage::") + where + " - image is not open");
    }
    return *_zarr_image;
}

bool CartaZarrImage::Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
    const carta::zarr::ReadOptions& options, const carta::zarr::ProgressCallback& progress) const {
    const auto& image = Opened("Read");
    buffer.resize(section.length());

    bool delete_storage(false);
    float* storage = buffer.getStorage(delete_storage);
    auto read = image.Read(_axes->Request(section),
        {storage, static_cast<std::size_t>(buffer.nelements())}, options, progress);
    buffer.putStorage(storage, delete_storage);
    return Finished(read, "Read");
}

bool CartaZarrImage::ComputeHistogram(const carta::zarr::HistogramRequest& request,
    const carta::zarr::HistogramSink& sink, const carta::zarr::ReadOptions& options) const {
    return Finished(Opened("ComputeHistogram").ComputeHistogram(request, sink, options), "ComputeHistogram");
}

bool CartaZarrImage::ComputeCubeHistogram(const carta::zarr::CubeHistogramRequest& request,
    carta::zarr::CubeHistogramResult& result, const carta::zarr::ReadOptions& options) const {
    auto computed = Opened("ComputeCubeHistogram").ComputeCubeHistogram(request, options);
    if (!Finished(computed, "ComputeCubeHistogram")) {
        return false;
    }
    result = std::move(computed).value();
    return true;
}

bool CartaZarrImage::ReduceSpectral(const carta::zarr::SpectralReduceRequest& request, const carta::zarr::SpectralSink& sink,
    const carta::zarr::ReadOptions& options) const {
    return Finished(Opened("ReduceSpectral").ReduceSpectral(request, sink, options), "ReduceSpectral");
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

// carta-zarr returns a flagged or absent pixel as NaN, so the mask this image reports is the
// finiteness of its own pixels, exactly as CartaFitsImage does for a float image. casacore's
// statistics and moments have no NaN handling of their own; they exclude a pixel only when the
// image reports a mask, so reporting none makes every one of those results NaN.
casacore::Bool CartaZarrImage::isMasked() const {
    return true;
}

casacore::Bool CartaZarrImage::hasPixelMask() const {
    return true;
}

const casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() const {
    throw casacore::AipsError("CartaZarrImage::pixelMask - the mask is read by section, not as a lattice");
}

casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() {
    throw casacore::AipsError("CartaZarrImage::pixelMask - the mask is read by section, not as a lattice");
}

casacore::Bool CartaZarrImage::doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) {
    {
        std::scoped_lock lock(_mask_cache_mutex);
        if (!_mask_cache.empty() && _mask_cache_start == section.start() && _mask_cache_length == section.length() &&
            _mask_cache_stride == section.stride()) {
            buffer.resize(section.length());
            buffer = _mask_cache;
            return false;
        }
    }

    casacore::Array<float> pixels;
    Read(pixels, section);
    buffer.resize(section.length());
    buffer = isFinite(pixels);
    return false;
}

std::vector<StorageEntry> CartaZarrImage::GetStorageInfo() const {
    if (!_zarr_image) {
        return {};
    }

    // An open image always has a layout, so there is nothing else to guard. That was not always so:
    // ImageDescriptor::storage used to be optional, and this asked whether it was there, because a
    // ChunkGeometry built without one reported the whole image as a single chunk. carta-zarr now
    // refuses such an array at the parse instead, so the case cannot reach an opened image.
    //
    // The layout is read from the geometry: it reports the same four facts in the image's own axis
    // order, which is the order this panel wants. Reading them from the descriptor meant undoing a
    // transpose here that the library had already undone -- the thing ChunkGeometry says in its own
    // comment a consumer should never have to do.
    const auto& storage = _zarr_image->chunk_geometry();

    // The values alone: these are the image's own axes in the image's own order, so the file-info
    // panel labels them the way it labels the image shape rather than naming them a second way.
    const auto format_shape = [](const std::vector<std::uint64_t>& values) {
        std::string result = "[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += std::to_string(values[index]);
        }
        return result + "]";
    };

    std::vector<StorageEntry> entries;
    if (storage.sharded) {
        entries.push_back({"Shard shape", format_shape(_axes->InCartaOrder(storage.shard_shape)), true});
    }
    entries.push_back({"Chunk shape", format_shape(_axes->InCartaOrder(storage.chunk_shape)), true});
    if (!storage.compressor.empty()) {
        entries.push_back({"Compressor", storage.compressor, false});
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

    // Fill a matrix and hand the whole thing over at once. casacore recomputes the smallest- and
    // largest-area beams only when the set is given all its beams together; setting them one at a
    // time leaves the smallest one at the null beam the set was sized with.
    //
    // Sized from the image, not from the table. casacore checks a beam set against the coordinate
    // axes (ImageInfo::checkBeamSet), so a table that stops short of the last channel would give a
    // set the image rejects -- opening the file would fail outright rather than degrade. Sized this
    // way, the planes the table does not name keep the null beam, which is the documented
    // degradation, and the warning below is what says so.
    const auto channels = _shape.size() > 2 ? static_cast<casacore::uInt>(_shape(2)) : casacore::uInt(1);
    const auto polarizations = _shape.size() > 3 ? static_cast<casacore::uInt>(_shape(3)) : casacore::uInt(1);
    if (max_channel >= channels || max_polarization >= polarizations) {
        // Beams for planes that are not there. Nothing sensible to put them on, and writing them
        // would run off the matrix.
        spdlog::warn("XRADIO beam table of {} names channel {} polarization {} of an image with {} and {}; ignoring the beams",
            _filename, max_channel, max_polarization, channels, polarizations);
        return;
    }
    casacore::Matrix<casacore::GaussianBeam> beam_matrix(channels, polarizations);
    std::size_t filled = 0;
    const carta::zarr::Beam* first = nullptr;
    bool every_plane_alike = true;
    for (const auto& beam : values) {
        if (beam.time != 0) {
            continue;
        }
        beam_matrix(static_cast<casacore::uInt>(beam.channel), static_cast<casacore::uInt>(beam.polarization)) =
            casacore::GaussianBeam(casacore::Quantity(beam.major, beam.unit), casacore::Quantity(beam.minor, beam.unit),
                casacore::Quantity(beam.position_angle, beam.unit));
        ++filled;

        if (first == nullptr) {
            first = &beam;
        } else if (beam.major != first->major || beam.minor != first->minor ||
                   beam.position_angle != first->position_angle || beam.unit != first->unit) {
            every_plane_alike = false;
        }
    }
    const auto planes = static_cast<std::size_t>(channels) * polarizations;
    if (filled != planes) {
        // The planes left over keep the null beam, which is what the smallest-area beam then reports.
        spdlog::warn("XRADIO beam table of {} covers {} of {} planes", _filename, filled, planes);
    }

    auto image_info = imageInfo();

    // A table that says the same beam on every plane is a single beam, and saying so is not
    // cosmetic. hasMultipleBeams() is what decides whether a consumer has to reconcile planes that
    // differ: ImageMoments convolves the whole cube to a common beam before it computes anything
    // (ImageMoments.tcc:74), materialising a full-size copy of the input. On a 7763 x 4742 x 7776
    // cube that copy is 1.09 TiB and the moment fails outright for want of a work directory --
    // for a convolution that, when every plane already has the same beam, changes nothing.
    //
    // Exactly equal, with no tolerance. Beams that merely almost agree are the case the common-beam
    // machinery exists for, and a tolerance invented here would quietly skip it.
    if (every_plane_alike && first != nullptr && filled == planes) {
        image_info.setBeams(casacore::ImageBeamSet(beam_matrix(0, 0)));
    } else {
        image_info.setBeams(casacore::ImageBeamSet(beam_matrix));
    }
    setImageInfo(image_info);
}

}  // namespace carta
