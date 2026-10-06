/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

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

CartaZarrImage::CartaZarrImage(const std::string& filename, const std::string& image_id) : _filename(filename) {
    // The store the file list already opened
    const auto look = ZarrStores::Instance().Look(filename);
    if (!look->dataset) {
        throw casacore::AipsError(
            "Failed to open XRADIO dataset: " + (look->why_not_openable.empty() ? "not an image store" : look->why_not_openable));
    }

    // Without an id, the first image offered, which the file list shows first
    if (image_id.empty() && look->offered.empty()) {
        throw casacore::AipsError(look->why_not_openable);
    }
    const auto& id = image_id.empty() ? look->offered.front() : image_id;

    auto image = look->dataset->OpenImage(id);
    if (!image) {
        throw casacore::AipsError("Failed to open XRADIO image '" + id + "': " + image.error().message);
    }
    _zarr_image = std::move(image.value());

    std::string reason;
    _axes = CartaZarrAxes::FromImage(_zarr_image->descriptor(), reason);
    if (!_axes) {
        throw casacore::AipsError(reason);
    }

    SetUpImage();
}

casacore::String CartaZarrImage::imageType() const {
    return "CartaZarrImage";
}

casacore::String CartaZarrImage::name(bool stripPath) const {
    return stripPath ? casacore::Path(_filename).baseName() : _filename;
}

casacore::IPosition CartaZarrImage::shape() const {
    return _axes->Shape();
}

casacore::Bool CartaZarrImage::ok() const {
    return coordinates().nPixelAxes() == _axes->Shape().size();
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
        entries.push_back({"Shard shape", format_shape(_axes->InCartaOrder(storage.shard_shape)), true});
    }
    entries.push_back({"Chunk shape", format_shape(_axes->InCartaOrder(storage.chunk_shape)), true});
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
        image_info.setImageType(casacore::ImageInfo::Intensity);
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

    const auto& shape = _axes->Shape();
    const auto channels = static_cast<casacore::uInt>(shape(2));
    const auto polarizations = static_cast<casacore::uInt>(shape(3));
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
