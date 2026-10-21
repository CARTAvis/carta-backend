/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrStores.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

#include <casacore/casa/OS/File.h>
#include <spdlog/spdlog.h>

#include "ZarrContext.h"

namespace fs = std::filesystem;

namespace carta {

namespace {

// CARTA's axes, which must also be the first axes of the image's logical order
constexpr std::array<carta::zarr::AxisRole, 4> CARTA_AXIS_ROLES{carta::zarr::AxisRole::spatial_x, carta::zarr::AxisRole::spatial_y,
    carta::zarr::AxisRole::spectral, carta::zarr::AxisRole::polarization};

std::string AxisNames(const std::vector<carta::zarr::AxisDescriptor>& axes) {
    std::string names;
    for (const auto& axis : axes) {
        names += (names.empty() ? "" : ", ") + axis.name;
    }
    return names;
}

// Absolute path with symlinks resolved, so that two names for one store share an entry
std::string KeyFor(const std::string& path) {
    std::error_code error;
    const auto resolved = fs::canonical(fs::path(path), error);
    return error ? path : resolved.string();
}

std::int64_t DirectoryTime(const std::string& path) {
    std::error_code error;
    const auto time = fs::last_write_time(fs::path(path), error);
    return error ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace

std::optional<casacore::IPosition> CartaShape(const std::vector<carta::zarr::AxisDescriptor>& image_axes, std::string& reason) {
    // Reads come back in the image's logical order, so it has to be CARTA's order, with time last
    const auto rank = image_axes.size();
    bool in_carta_order = rank == CARTA_AXIS_ROLES.size() || rank == CARTA_AXIS_ROLES.size() + 1;
    for (std::size_t index = 0; in_carta_order && index < CARTA_AXIS_ROLES.size(); ++index) {
        in_carta_order = image_axes[index].role == CARTA_AXIS_ROLES[index];
    }
    if (in_carta_order && rank > CARTA_AXIS_ROLES.size()) {
        in_carta_order = image_axes.back().role == carta::zarr::AxisRole::time;
    }
    if (!in_carta_order) {
        reason = "XRADIO image axes (" + AxisNames(image_axes) + ") are not spatial X, spatial Y, spectral, polarization and time";
        return std::nullopt;
    }
    if (rank > CARTA_AXIS_ROLES.size() && image_axes.back().length != 1) {
        reason = "CARTA backend currently supports only XRADIO images with a singleton time axis";
        return std::nullopt;
    }

    casacore::IPosition shape(CARTA_AXIS_ROLES.size(), 0);
    for (std::size_t index = 0; index < CARTA_AXIS_ROLES.size(); ++index) {
        const auto& axis = image_axes[index];
        if (axis.length == 0) {
            reason = "XRADIO image has an empty axis: " + axis.name;
            return std::nullopt;
        }
        // An axis one pixel across has no pixel scale, and casacore refuses a direction increment of zero
        if (index < 2 && axis.length == 1) {
            reason = "XRADIO image has a spatial axis one pixel across, which has no pixel scale: " + axis.name;
            return std::nullopt;
        }
        if (axis.length > static_cast<std::uint64_t>(std::numeric_limits<casacore::Int>::max())) {
            reason = "XRADIO image dimension is too large for CARTA";
            return std::nullopt;
        }
        shape[index] = static_cast<casacore::Int>(axis.length);
    }
    return shape;
}

std::optional<casacore::IPosition> CartaShape(const carta::zarr::ImageDescriptor& descriptor, std::string& reason) {
    auto shape = CartaShape(descriptor.axes, reason);
    if (!shape) {
        return std::nullopt;
    }
    if (!(descriptor.direction || descriptor.aperture) || !descriptor.spectral || !descriptor.polarization) {
        reason = "XRADIO image is missing a required coordinate descriptor";
        return std::nullopt;
    }
    // Equal samples along a spatial axis also leave no pixel scale, on the sky or the aperture plane
    const auto& increments = descriptor.direction ? descriptor.direction->increment : descriptor.aperture->increment;
    for (const double increment : increments) {
        if (increment == 0.0) {
            reason = std::string("XRADIO image has ") + (descriptor.direction ? "a direction" : "an aperture") +
                     " axis with no increment, so no pixel scale";
            return std::nullopt;
        }
    }
    return shape;
}

ZarrOffer OfferedImages(const carta::zarr::DatasetDescriptor& dataset) {
    ZarrOffer offer;
    std::string carta_refusal;
    std::string library_refusal;
    for (const auto& entry : dataset.images) {
        if (!entry.openable) {
            // carta-zarr gives the reason in its first diagnostic
            auto reason = entry.diagnostics.empty() ? std::string("carta-zarr will not open it") : entry.diagnostics.front().message;
            if (library_refusal.empty()) {
                library_refusal = reason;
            }
            offer.refused.push_back({entry.id, std::move(reason)});
            continue;
        }
        std::string reason;
        if (CartaShape(entry.axes, reason)) {
            offer.ids.push_back(entry.id);
            continue;
        }
        if (carta_refusal.empty()) {
            carta_refusal = reason;
        }
        offer.refused.push_back({entry.id, std::move(reason)});
    }
    if (offer.ids.empty()) {
        if (!carta_refusal.empty()) {
            offer.why_none = carta_refusal;
        } else if (!library_refusal.empty()) {
            offer.why_none = library_refusal;
        } else {
            offer.why_none = "XRADIO dataset contains no image variables";
        }
    }
    return offer;
}

ZarrStores::ZarrStores(std::size_t capacity, std::chrono::seconds ttl) : _capacity(std::max<std::size_t>(capacity, 1)), _ttl(ttl) {}

ZarrStores& ZarrStores::Instance() {
    static ZarrStores stores(ZARR_STORES_SIZE, ZARR_STORES_TTL);
    return stores;
}

std::shared_ptr<const ZarrStoreLook> ZarrStores::Look(const std::string& path) {
    const auto key = KeyFor(path);
    const auto directory_mtime = DirectoryTime(key);
    {
        std::scoped_lock lock(_mutex);
        auto found = _records.find(key);
        if (found != _records.end()) {
            if ((Clock::now() - found->second.looked_at) <= _ttl && found->second.directory_mtime == directory_mtime) {
                _queue.remove(key);
                _queue.push_front(key);
                return found->second.look;
            }
            _records.erase(found);
            _queue.remove(key);
        }
    }

    // Opened without the lock, since it walks the store; if two callers open the same path, the later one is kept
    auto look = LookAt(key);

    std::scoped_lock lock(_mutex);
    if (_records.find(key) == _records.end()) {
        if (_records.size() >= _capacity) {
            _records.erase(_queue.back());
            _queue.pop_back();
        }
        _queue.push_front(key);
    }
    _records[key] = Record{directory_mtime, Clock::now(), look};
    return look;
}

std::shared_ptr<const ZarrStoreLook> ZarrStores::LookAt(const std::string& path) {
    auto look = std::make_shared<ZarrStoreLook>();
    look->path = path;
    std::error_code error;
    // Only a directory can be a store
    if (!fs::is_directory(fs::path(path), error)) {
        return look;
    }

    auto dataset = carta::zarr::Dataset::Open(GetZarrContext(), path);
    if (!dataset) {
        const auto code = dataset.error().code;
        // Nothing there, not Zarr, or not an image store
        if (code == carta::zarr::ErrorCode::not_found || code == carta::zarr::ErrorCode::not_zarr ||
            code == carta::zarr::ErrorCode::unsupported_schema) {
            return look;
        }
        // A store that would not open is a malformed image only if the XRADIO profile recognises it
        const auto probed = carta::zarr::ProbeSchema(path, carta::zarr::kXradioImageSchema);
        if (probed && probed.value().kind == carta::zarr::SchemaMatchKind::invalid) {
            look->is_zarr = true;
            look->why_not_openable = dataset.error().message;
        }
        return look;
    }

    look->is_zarr = true;
    auto offer = OfferedImages(dataset.value().descriptor());
    for (const auto& refused : offer.refused) {
        spdlog::debug("Not offering Zarr image {} in {}: {}", refused.id, path, refused.reason);
    }
    look->offered = std::move(offer.ids);
    look->why_not_openable = std::move(offer.why_none);
    look->dataset = std::move(dataset.value());
    return look;
}

ZarrStoreSize ZarrStoreLook::Size() const {
    std::scoped_lock lock(_size_mutex);
    if (_size) {
        return *_size;
    }

    // A store that will not open is not measured, since walking a large one has no time limit
    ZarrStoreSize size;
    if (dataset) {
        size.bytes = casacore::File(path).size();
        auto measured = dataset->Size();
        if (measured && measured.value().bytes <= static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
            size.bytes = static_cast<int64_t>(measured.value().bytes);
            size.declared = measured.value().basis == carta::zarr::SizeBasis::declared;
        }
    }
    _size = size;
    return size;
}

std::optional<ZarrOpenedImage> ZarrStoreLook::OpenImage(const std::string& id, std::string& reason) const {
    if (!dataset) {
        reason = "Failed to open XRADIO dataset: " + (why_not_openable.empty() ? std::string("not an image store") : why_not_openable);
        return std::nullopt;
    }
    // Without an id, the first image offered, which the file list shows first
    if (id.empty() && offered.empty()) {
        reason = why_not_openable;
        return std::nullopt;
    }
    const auto& image_id = id.empty() ? offered.front() : id;

    auto image = dataset->OpenImage(image_id);
    if (!image) {
        reason = "Failed to open XRADIO image '" + image_id + "': " + image.error().message;
        return std::nullopt;
    }
    auto shape = CartaShape(image.value().descriptor(), reason);
    if (!shape) {
        return std::nullopt;
    }
    return ZarrOpenedImage{std::move(image.value()), *shape};
}

void ZarrStores::Clear() {
    std::scoped_lock lock(_mutex);
    _records.clear();
    _queue.clear();
}

} // namespace carta
