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

std::optional<CartaZarrAxes> CartaZarrAxes::Of(const carta::zarr::ImageDescriptor& descriptor, std::string& reason) {
    CartaZarrAxes axes;
    axes._rank = descriptor.axes.size();
    axes._shape = casacore::IPosition(4, 0);
    std::array<bool, 4> seen{};

    for (std::size_t index = 0; index < descriptor.axes.size(); ++index) {
        const auto& axis = descriptor.axes[index];
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
    if (!descriptor.direction || !descriptor.spectral || !descriptor.polarization) {
        reason = "XRADIO image is missing a required coordinate descriptor";
        return std::nullopt;
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

}  // namespace carta
