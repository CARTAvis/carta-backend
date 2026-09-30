/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include "ZarrCasacore.h"
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

// What casacore hears of a read that did not succeed: an exception, which is how a casacore image
// reports failure. A cancellation is thrown as well. Neither override hands the read a way to stop,
// so none can arrive today; if one did, the buffer would hold a prefix, and passing that to casacore
// as the section's pixels would be worse than saying so.
void ThrowIfUnread(const carta::zarr::Result<std::size_t>& read, const char* where) {
    if (!read) {
        throw casacore::AipsError(std::string("CartaZarrImage::") + where + " - " + read.error().message);
    }
}

}  // namespace

CartaZarrImage::CartaZarrImage(const std::string& filename, const std::string& image_id) : _filename(filename) {
    const auto context = GetZarrContext();
    auto dataset = carta::zarr::Dataset::Open(context, filename);
    if (!dataset) {
        throw casacore::AipsError("Failed to open XRADIO dataset: " + dataset.error().message);
    }

    if (image_id.empty()) {
        // The first image CARTA can display, in the library's order, which puts SKY first. The
        // library's own default is the first image it will open, and CARTA refuses more than the
        // library does, so the two can differ. What is said when none will do is why the first one
        // would not.
        //
        // Decided from the listing's axes, as the file list decides what to offer, so that the image
        // opened here is the first one the file list offered.
        std::string refusal;
        for (const auto& entry : dataset.value().descriptor().images) {
            if (!entry.openable) {
                continue;
            }
            std::string reason;
            if (CartaZarrAxes::Of(entry.axes, reason)) {
                _image_id = entry.id;
                break;
            }
            if (refusal.empty()) {
                refusal = reason;
            }
        }
        if (_image_id.empty()) {
            throw casacore::AipsError(refusal.empty() ? "XRADIO dataset contains no image variables" : refusal);
        }
    } else {
        _image_id = image_id;
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
      _notes(other._notes),
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
    ThrowIfUnread(Read(buffer, section), "doGetSlice");

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

carta::zarr::Result<std::size_t> CartaZarrImage::Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
    const carta::zarr::ReadOptions& options, const carta::zarr::ProgressCallback& progress) const {
    const auto& image = Opened("Read");
    buffer.resize(section.length());

    bool delete_storage(false);
    float* storage = buffer.getStorage(delete_storage);
    auto read = image.Read(_axes->Request(section),
        {storage, static_cast<std::size_t>(buffer.nelements())}, options, progress);
    buffer.putStorage(storage, delete_storage);
    return read;
}

const carta::zarr::Image& CartaZarrImage::Library() const {
    return Opened("Library");
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
    ThrowIfUnread(Read(pixels, section), "doGetMaskSlice");
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
        _notes = NotesFromDiagnostics(_descriptor.diagnostics);
        for (const auto& note : _notes) {
            if (note.topic == ZarrNoteTopic::none) {
                // Nothing in the file info it qualifies, so this is the only place it is said.
                spdlog::warn("XRADIO {}: {}", _filename, note.detail);
            }
        }
        auto made = MakeZarrCoordinateSystem(_descriptor);
        for (auto& note : made.notes) {
            spdlog::warn("XRADIO {}: {}", _filename, note.detail);
            _notes.push_back(std::move(note));
        }
        setCoordinateInfo(made.coordinates);
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

    // Sized from the image rather than from the table; see MakeZarrBeamSet.
    const auto channels = _shape.size() > 2 ? static_cast<casacore::uInt>(_shape(2)) : casacore::uInt(1);
    const auto polarizations = _shape.size() > 3 ? static_cast<casacore::uInt>(_shape(3)) : casacore::uInt(1);
    auto made = MakeZarrBeamSet(beams.value(), channels, polarizations);
    for (auto& note : made.notes) {
        spdlog::warn("XRADIO {}: {}", _filename, note.detail);
        _notes.push_back(std::move(note));
    }
    if (!made.beams) {
        return;
    }
    auto image_info = imageInfo();
    image_info.setBeams(*made.beams);
    setImageInfo(image_info);
}

}  // namespace carta
