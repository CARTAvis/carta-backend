/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/images/Images/ImageInfo.h>
#include <spdlog/spdlog.h>

#include "ZarrCasacore.h"
#include "ZarrStores.h"

namespace carta {
namespace {

// CARTA's axes: spatial X, spatial Y, spectral, polarization. An image's logical order is these, then at
// most a time axis of length one; see CartaShape.
constexpr std::size_t CARTA_AXES = 4;

// Per-axis values in the image's order, such as a chunk shape, in CARTA's four axes: the time axis, last,
// is left out, and a missing axis is 1
std::vector<std::uint64_t> InCartaOrder(const std::vector<std::uint64_t>& values) {
    std::vector<std::uint64_t> result(CARTA_AXES, 1);
    std::copy_n(values.begin(), std::min(values.size(), result.size()), result.begin());
    return result;
}

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

casacore::Bool CartaZarrImage::doGetSlice(casacore::Array<float>& /*buffer*/, const casacore::Slicer& /*section*/) {
    throw casacore::AipsError("CartaZarrImage::doGetSlice - reading Zarr pixels is not supported yet");
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

std::vector<StorageEntry> CartaZarrImage::GetStorageInfo() const {
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
        _notes = NotesFromDiagnostics(descriptor.diagnostics);
        for (const auto& note : _notes) {
            if (note.topic == ZarrNoteTopic::none) {
                // No file info entry to show it beside, so it is only logged
                spdlog::warn("XRADIO {}: {}", _filename, note.detail);
            }
        }
        auto made = MakeZarrCoordinateSystem(descriptor);
        for (auto& note : made.notes) {
            spdlog::warn("XRADIO {}: {}", _filename, note.detail);
            _notes.push_back(std::move(note));
        }
        setCoordinateInfo(made.coordinates);
        if (!descriptor.unit.empty()) {
            setUnits(casacore::Unit(descriptor.unit));
        }

        casacore::ImageInfo image_info;
        image_info.setImageType(MakeZarrImageType(descriptor.sub_type));
        if (descriptor.observation && !descriptor.observation->object_name.empty()) {
            image_info.setObjectName(casacore::String(descriptor.observation->object_name));
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
    auto beams = _zarr_image->ReadBeams();
    if (!beams) {
        spdlog::warn("Failed to read XRADIO image beams from {}: {}", _filename, beams.error().message);
        return;
    }
    if (beams.value().empty()) {
        return;
    }

    const auto channels = static_cast<casacore::uInt>(_shape(2));
    const auto polarizations = static_cast<casacore::uInt>(_shape(3));
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

} // namespace carta
