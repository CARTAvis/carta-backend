/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/images/Images/ImageInfo.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "ZarrCasacore.h"
#include "ZarrStores.h"

namespace carta {
namespace {

// CARTA's axes: spatial X, spatial Y, spectral, polarization. An image's logical order is these, then at
// most a time axis of length one; see CartaShape.
constexpr std::size_t CARTA_AXES = 4;

// Per-axis values, such as a chunk shape, cut to CARTA's four axes. They are in the image's order, which
// starts with CARTA's axes, so this drops a trailing time axis and fills a missing axis with 1.
std::vector<std::uint64_t> InCartaOrder(const std::vector<std::uint64_t>& values) {
    std::vector<std::uint64_t> result(CARTA_AXES, 1);
    std::copy_n(values.begin(), std::min(values.size(), result.size()), result.begin());
    return result;
}

// A read of `section`, which is in CARTA's order, as a carta-zarr request for an image of `rank` axes; a
// time axis is read at its one time
carta::zarr::ReadRequest RequestFor(const casacore::Slicer& section, std::size_t rank) {
    carta::zarr::ReadRequest request;
    for (casacore::uInt axis = 0; axis < CARTA_AXES; ++axis) {
        request.axes.push_back({static_cast<std::uint64_t>(section.start()(axis)), static_cast<std::uint64_t>(section.length()(axis)),
            static_cast<std::uint64_t>(section.stride()(axis))});
    }
    if (rank > CARTA_AXES) {
        request.axes.push_back({0, 1, 1});
    }
    return request;
}

// A section this size or smaller keeps its mask for the mask read that follows, which casacore's cube walks
// and CARTA's own reads always make: reading the section again would decode it twice. The mask takes a
// quarter of what the section's pixels did.
constexpr casacore::Int MASK_CACHE_MAX_PIXELS = 1 << 30;

// casacore hears of a failed read as an exception
void ThrowIfUnread(const carta::zarr::Result<std::size_t>& read, const char* where) {
    if (!read) {
        throw casacore::AipsError(std::string("CartaZarrImage::") + where + " - " + read.error().message);
    }
}

// The mask of a Zarr image as a read-only lattice. It holds its own copy of the image, since casacore keeps
// clones of it after the image it came from is gone.
class ZarrPixelMask : public casacore::Lattice<casacore::Bool> {
public:
    explicit ZarrPixelMask(std::shared_ptr<CartaZarrImage> image) : _image(std::move(image)) {}

    casacore::Lattice<casacore::Bool>* clone() const override {
        return new ZarrPixelMask(*this);
    }
    casacore::IPosition shape() const override {
        return _image->shape();
    }
    casacore::Bool isWritable() const override {
        return false;
    }
    casacore::Bool doGetSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) override {
        return _image->doGetMaskSlice(buffer, section);
    }
    void doPutSlice(const casacore::Array<casacore::Bool>& /*buffer*/, const casacore::IPosition& /*where*/,
        const casacore::IPosition& /*stride*/) override {
        throw casacore::AipsError("ZarrPixelMask::doPutSlice - the mask is not writable");
    }

private:
    std::shared_ptr<CartaZarrImage> _image;
};

} // namespace

CartaZarrImage::CartaZarrImage(const std::string& filename, const std::string& image_id) : _filename(filename) {
    // From the store the file list already opened
    std::string reason;
    auto opened = ZarrStores::Instance().Probe(filename)->OpenImage(image_id, reason);
    if (!opened) {
        throw casacore::AipsError(reason);
    }
    _zarr_image = std::move(opened->image);
    _shape = opened->shape;

    SetUpImage();
}

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
    return coordinates().nPixelAxes() == _shape.size();
}

casacore::DataType CartaZarrImage::dataType() const {
    // Pixels are read as float whatever type is stored; InternalDataType is the stored type
    return casacore::TpFloat;
}

casacore::DataType CartaZarrImage::InternalDataType() const {
    switch (_zarr_image->descriptor().stored_type) {
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

casacore::IPosition CartaZarrImage::ChunkShape() const {
    const auto chunk = InCartaOrder(_zarr_image->chunk_geometry().chunk_shape);
    casacore::IPosition shape(chunk.size());
    for (casacore::uInt axis = 0; axis < chunk.size(); ++axis) {
        shape(axis) = static_cast<ssize_t>(chunk[axis]);
    }
    return shape;
}

// max_pixels is followed only as far as it helps: a cursor smaller than a chunk decodes the whole chunk
// anyway, so the non-spatial axes are collapsed to fit, and the spatial chunk is kept even when it holds
// more than max_pixels
casacore::IPosition CartaZarrImage::doNiceCursorShape(casacore::uInt max_pixels) const {
    const auto chunk = ChunkShape();
    // Clipped to the image in IPosition's 64-bit width: a chunk may be declared longer than its array. Every
    // axis of an image CARTA displays is at least one long; see CartaShape.
    casacore::IPosition cursor(_shape.size());
    for (casacore::uInt axis = 0; axis < _shape.size(); ++axis) {
        cursor(axis) = std::clamp<ssize_t>(chunk(axis), 1, _shape(axis));
    }
    const auto most_pixels = static_cast<ssize_t>(max_pixels);

    // Collapse the non-spatial axes, slowest first, until the cursor fits in max_pixels
    for (casacore::uInt axis = _shape.size(); axis > 2 && cursor.product() > most_pixels; --axis) {
        cursor(axis - 1) = 1;
    }
    // Grow by whole chunks while the cursor still fits, so that each read covers as many chunks as allowed
    for (casacore::uInt axis = 0; axis < 2; ++axis) {
        const auto step = cursor(axis);
        while (cursor(axis) + step <= _shape(axis) && (cursor.product() / cursor(axis)) * (cursor(axis) + step) <= most_pixels) {
            cursor(axis) += step;
        }
    }
    return cursor;
}

carta::zarr::Result<std::size_t> CartaZarrImage::Read(casacore::Array<float>& buffer, const casacore::Slicer& section) const {
    if (!section.isFixed() || section.ndim() != _shape.size()) {
        throw casacore::AipsError("CartaZarrImage::Read - the section must be fixed and have the image's rank");
    }
    buffer.resize(section.length());
    bool delete_storage(false);
    float* storage = buffer.getStorage(delete_storage);
    auto read = _zarr_image->Read(
        RequestFor(section, _zarr_image->descriptor().axes.size()), {storage, static_cast<std::size_t>(buffer.nelements())});
    buffer.putStorage(storage, delete_storage);
    return read;
}

casacore::Bool CartaZarrImage::doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) {
    ThrowIfUnread(Read(buffer, section), "doGetSlice");

    const bool keep = section.length().product() <= MASK_CACHE_MAX_PIXELS;
    casacore::Array<casacore::Bool> mask;
    if (keep) {
        mask.reference(isFinite(buffer));
    }
    std::scoped_lock lock(_mask_cache.mutex);
    if (keep) {
        _mask_cache.section = section;
        _mask_cache.mask.reference(mask);
    } else {
        // A section too large to keep drops what an earlier one kept
        _mask_cache.section.reset();
        _mask_cache.mask.resize();
    }
    return false;
}

void CartaZarrImage::doPutSlice(
    const casacore::Array<float>& /*buffer*/, const casacore::IPosition& /*where*/, const casacore::IPosition& /*stride*/) {
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
    return true;
}

casacore::Bool CartaZarrImage::hasPixelMask() const {
    return true;
}

const casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() const {
    std::scoped_lock lock(_mask_cache.mutex);
    if (!_mask_cache.lattice) {
        _mask_cache.lattice = std::make_unique<ZarrPixelMask>(std::make_shared<CartaZarrImage>(*this));
    }
    return *_mask_cache.lattice;
}

casacore::Lattice<casacore::Bool>& CartaZarrImage::pixelMask() {
    return const_cast<casacore::Lattice<casacore::Bool>&>(std::as_const(*this).pixelMask());
}

casacore::Bool CartaZarrImage::doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) {
    {
        // The mask of the section just read, which is let go once read: a cube walk's last one may be a gibibyte.
        // Compared by start, length and stride, since a casacore iterator asks for the pixels by length and
        // the mask by end.
        std::scoped_lock lock(_mask_cache.mutex);
        const auto& kept = _mask_cache.section;
        if (kept && kept->start().isEqual(section.start()) && kept->length().isEqual(section.length()) &&
            kept->stride().isEqual(section.stride())) {
            buffer.resize(section.length());
            buffer = _mask_cache.mask;
            _mask_cache.section.reset();
            _mask_cache.mask.resize();
            return false;
        }
    }

    casacore::Array<float> pixels;
    ThrowIfUnread(Read(pixels, section), "doGetMaskSlice");
    buffer.resize(section.length());
    buffer = isFinite(pixels);
    return false;
}

std::size_t CartaZarrImage::MaskCacheBytes() const {
    std::scoped_lock lock(_mask_cache.mutex);
    return _mask_cache.mask.nelements() * sizeof(casacore::Bool);
}

std::vector<StorageEntry> CartaZarrImage::GetStorageInfo() const {
    const auto format_shape = [](const std::vector<std::uint64_t>& values) { return fmt::format("[{}]", fmt::join(values, ", ")); };

    const auto& storage = _zarr_image->chunk_geometry();
    std::vector<StorageEntry> entries;
    if (storage.sharded) {
        entries.push_back({"Shard shape", format_shape(InCartaOrder(storage.shard_shape)), true});
    }
    entries.push_back({"Chunk shape", format_shape(InCartaOrder(storage.chunk_shape)), true});
    if (!storage.compressor.empty()) {
        entries.push_back({"Compressor", storage.compressor, false});
    }
    return entries;
}

void CartaZarrImage::SetUpImage() {
    const auto& descriptor = _zarr_image->descriptor();
    try {
        AddNotes(NotesFromDiagnostics(descriptor.diagnostics));
        auto made = MakeZarrCoordinateSystem(descriptor);
        AddNotes(std::move(made.notes));
        setCoordinateInfo(made.coordinates);
        if (!descriptor.unit.empty()) {
            setUnits(casacore::Unit(descriptor.unit));
        }

        casacore::ImageInfo image_info;
        image_info.setImageType(MakeZarrImageType(descriptor.sub_type));
        if (descriptor.observation && !descriptor.observation->object_name.empty()) {
            image_info.setObjectName(casacore::String(descriptor.observation->object_name));
        }
        if (auto beams = ReadBeamSet()) {
            image_info.setBeams(*beams);
        }
        setImageInfo(image_info);
    } catch (const casacore::AipsError&) {
        throw;
    } catch (const std::exception& error) {
        spdlog::error("Error opening XRADIO image {}: {}", _filename, error.what());
        throw casacore::AipsError("Error opening XRADIO image: " + std::string(error.what()));
    }
}

std::optional<casacore::ImageBeamSet> CartaZarrImage::ReadBeamSet() {
    auto beams = _zarr_image->ReadBeams();
    if (!beams) {
        spdlog::warn("Failed to read XRADIO image beams from {}: {}", _filename, beams.error().message);
        return std::nullopt;
    }
    if (beams.value().empty()) {
        return std::nullopt;
    }

    const auto channels = static_cast<casacore::uInt>(_shape(2));
    const auto polarizations = static_cast<casacore::uInt>(_shape(3));
    auto made = MakeZarrBeamSet(beams.value(), channels, polarizations);
    AddNotes(std::move(made.notes));
    return made.beams;
}

void CartaZarrImage::AddNotes(std::vector<ZarrNote> notes) {
    // Every note is logged in full; the file info shows the brief form beside the value it is about
    for (auto& note : notes) {
        spdlog::warn("XRADIO {}: {}", _filename, note.detail);
        _notes.push_back(std::move(note));
    }
}

} // namespace carta
