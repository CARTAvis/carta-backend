/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaZarrAxes.h"

#include <algorithm>
#include <array>
#include <limits>

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

} // namespace

std::optional<CartaZarrAxes> CartaZarrAxes::FromListing(const std::vector<carta::zarr::AxisDescriptor>& image_axes, std::string& reason) {
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

    CartaZarrAxes axes;
    axes._shape = casacore::IPosition(CARTA_AXIS_ROLES.size(), 0);
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
        axes._shape[index] = static_cast<casacore::Int>(axis.length);
    }
    return axes;
}

std::optional<CartaZarrAxes> CartaZarrAxes::FromImage(const carta::zarr::ImageDescriptor& descriptor, std::string& reason) {
    auto axes = FromListing(descriptor.axes, reason);
    if (!axes) {
        return std::nullopt;
    }
    if (!descriptor.direction || !descriptor.spectral || !descriptor.polarization) {
        reason = "XRADIO image is missing a required coordinate descriptor";
        return std::nullopt;
    }
    // Equal direction cosines also leave no pixel scale
    for (const double increment : descriptor.direction->increment) {
        if (increment == 0.0) {
            reason = "XRADIO image has a direction axis with no increment, so no pixel scale";
            return std::nullopt;
        }
    }
    return axes;
}

std::vector<std::uint64_t> CartaZarrAxes::InCartaOrder(const std::vector<std::uint64_t>& values) {
    std::vector<std::uint64_t> result(CARTA_AXIS_ROLES.size(), 1);
    std::copy_n(values.begin(), std::min(values.size(), result.size()), result.begin());
    return result;
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
        if (CartaZarrAxes::FromListing(entry.axes, reason)) {
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

} // namespace carta
