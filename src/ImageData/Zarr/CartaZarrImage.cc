/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrImage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/Path.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/images/Images/ImageInfo.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "Util/Casacore.h"
#include "ZarrCasacore.h"
#include "ZarrStores.h"

namespace carta {
namespace {

// CARTA's axes: spatial X, spatial Y, spectral, polarization. An image's logical order is these, then at
// most a time axis of length one; see CartaShape in ZarrStores.h.
constexpr std::size_t CARTA_AXES = 4;

// Returns per-axis values, such as a chunk shape, for CARTA's four axes only.
// The values are in the image's axis order. That order starts with CARTA's four axes.
std::vector<std::uint64_t> InCartaOrder(const std::vector<std::uint64_t>& values) {
    std::vector<std::uint64_t> result(CARTA_AXES, 1);
    std::copy_n(values.begin(), std::min(values.size(), result.size()), result.begin());
    return result;
}

// Returns the image's unit as a casacore::Unit, or nothing if casacore does not know it.
//
// XRADIO copies the FITS BUNIT string into the store without changing it, so a unit can be written in
// FITS style, for example JY/BEAM. casacore::Unit does not accept that spelling. We fix it with
// NormalizeUnit, the same function FileLoader uses for every image, so JY/BEAM becomes Jy/beam.
// NormalizeUnit needs casacore's FITS unit table, so we load it first with UnitMap::addFITS. casacore
// loads the table only once and holds its own lock while doing so, so calling it for every image is safe.
//
// If the unit is still not valid, we leave the image without a unit and add a note. casacore's FITS
// reader would add the unknown unit to casacore's global unit map instead. We do not do that, because
// the map has no lock and other threads may be reading it at the same time.
std::optional<casacore::Unit> BrightnessUnit(const std::string& unit) {
    casacore::UnitMap::addFITS();
    casacore::String normalized(unit);
    NormalizeUnit(normalized);
    if (!casacore::UnitVal::check(normalized)) {
        return std::nullopt;
    }
    return casacore::Unit(normalized);
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
            if (const auto unit = BrightnessUnit(descriptor.unit)) {
                setUnits(*unit);
            } else {
                AddNotes({ZarrNote{ZarrNoteTopic::none, "unit omitted",
                    "unit " + descriptor.unit + " is one casacore does not know; the image is left without a unit"}});
            }
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
