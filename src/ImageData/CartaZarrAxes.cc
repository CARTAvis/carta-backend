/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrAxes.h"

#include <limits>

namespace carta {
namespace {

// Where each role sits in CARTA's order, or none for a role CARTA has no axis for.
std::optional<std::size_t> CartaAxisOf(carta::zarr::AxisRole role) {
    switch (role) {
        case carta::zarr::AxisRole::spatial_x:
            return 0;
        case carta::zarr::AxisRole::spatial_y:
            return 1;
        case carta::zarr::AxisRole::spectral:
            return 2;
        case carta::zarr::AxisRole::polarization:
            return 3;
        default:
            return std::nullopt;
    }
}

}  // namespace

std::optional<CartaZarrAxes> CartaZarrAxes::Of(const std::vector<carta::zarr::AxisDescriptor>& image_axes, std::string& reason) {
    CartaZarrAxes axes;
    axes._rank = image_axes.size();
    axes._shape = casacore::IPosition(4, 0);
    std::array<bool, 4> seen{};

    for (std::size_t index = 0; index < image_axes.size(); ++index) {
        const auto& axis = image_axes[index];
        if (axis.role == carta::zarr::AxisRole::time) {
            if (axes._time_index || axis.length != 1) {
                reason = "CARTA backend currently supports only XRADIO images with a singleton time axis";
                return std::nullopt;
            }
            axes._time_index = index;
            continue;
        }
        const auto carta_axis = CartaAxisOf(axis.role);
        if (!carta_axis) {
            reason = "XRADIO image has an axis CARTA does not display: " + axis.name;
            return std::nullopt;
        }
        if (seen[*carta_axis]) {
            reason = "XRADIO image has two axes playing the part of " + axis.name;
            return std::nullopt;
        }
        if (axis.length == 0) {
            reason = "XRADIO image has an empty axis: " + axis.name;
            return std::nullopt;
        }
        // One direction cosine is not a spacing, so an axis one pixel across has no pixel scale, and
        // casacore refuses a direction coordinate whose increment is zero. Refused here, from the axes
        // alone, so that the file list does not offer what would then fail to open.
        if ((axis.role == carta::zarr::AxisRole::spatial_x || axis.role == carta::zarr::AxisRole::spatial_y) && axis.length == 1) {
            reason = "XRADIO image has a spatial axis one pixel across, which has no pixel scale: " + axis.name;
            return std::nullopt;
        }
        if (axis.length > static_cast<std::uint64_t>(std::numeric_limits<casacore::Int>::max())) {
            reason = "XRADIO image dimension is too large for casacore";
            return std::nullopt;
        }
        seen[*carta_axis] = true;
        axes._own_index[*carta_axis] = index;
        axes._shape[*carta_axis] = static_cast<casacore::Int>(axis.length);
    }
    for (const bool found : seen) {
        if (!found) {
            reason = "XRADIO image lacks one of the spatial, spectral and polarization axes CARTA displays";
            return std::nullopt;
        }
    }
    return axes;
}

std::optional<CartaZarrAxes> CartaZarrAxes::Of(const carta::zarr::ImageDescriptor& descriptor, std::string& reason) {
    auto axes = Of(descriptor.axes, reason);
    if (!axes) {
        return std::nullopt;
    }
    if (!descriptor.direction || !descriptor.spectral || !descriptor.polarization) {
        reason = "XRADIO image is missing a required coordinate descriptor";
        return std::nullopt;
    }
    // The other way to the same thing, which only the coordinate values show: two direction cosines
    // that are the same. carta-zarr says so as a degenerate axis and leaves the increment at zero.
    for (const double increment : descriptor.direction->increment) {
        if (increment == 0.0) {
            reason = "XRADIO image has a direction axis with no increment, so no pixel scale";
            return std::nullopt;
        }
    }
    return axes;
}

carta::zarr::ReadRequest CartaZarrAxes::Request(const casacore::Slicer& section) const {
    const auto& start = section.start();
    const auto& length = section.length();
    const auto& stride = section.stride();

    carta::zarr::ReadRequest request;
    request.axes.resize(_rank);
    for (std::size_t carta_axis = 0; carta_axis < _own_index.size(); ++carta_axis) {
        const auto at = static_cast<casacore::uInt>(carta_axis);
        request.axes[_own_index[carta_axis]] = {static_cast<std::uint64_t>(start(at)), static_cast<std::uint64_t>(length(at)),
            static_cast<std::uint64_t>(stride(at))};
    }
    if (_time_index) {
        request.axes[*_time_index] = {0, 1, 1};
    }
    return request;
}

std::vector<std::uint64_t> CartaZarrAxes::InCartaOrder(const std::vector<std::uint64_t>& values) const {
    std::vector<std::uint64_t> result(_own_index.size(), 1);
    for (std::size_t carta_axis = 0; carta_axis < _own_index.size(); ++carta_axis) {
        if (_own_index[carta_axis] < values.size()) {
            result[carta_axis] = values[_own_index[carta_axis]];
        }
    }
    return result;
}

ZarrOffer OfferedImages(const carta::zarr::DatasetDescriptor& dataset) {
    ZarrOffer offer;
    std::string carta_refusal;
    std::string library_refusal;
    for (const auto& entry : dataset.images) {
        if (!entry.openable) {
            // The library says why in its diagnostics; the first is the one it leads with.
            auto reason = entry.diagnostics.empty() ? std::string("carta-zarr will not open it")
                                                    : entry.diagnostics.front().message;
            if (library_refusal.empty()) {
                library_refusal = reason;
            }
            offer.refused.push_back({entry.id, std::move(reason)});
            continue;
        }
        std::string reason;
        if (CartaZarrAxes::Of(entry.axes, reason)) {
            offer.ids.push_back(entry.id);
            continue;
        }
        if (carta_refusal.empty()) {
            carta_refusal = reason;
        }
        offer.refused.push_back({entry.id, std::move(reason)});
    }
    if (offer.ids.empty()) {
        offer.why_none = !carta_refusal.empty()    ? carta_refusal
                         : !library_refusal.empty() ? library_refusal
                                                    : std::string("XRADIO dataset contains no image variables");
    }
    return offer;
}

}  // namespace carta
