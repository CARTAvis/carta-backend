/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_

#include <carta-zarr/descriptor.h>
#include <carta-zarr/read.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <casacore/casa/aipstype.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Arrays/Slicer.h>

namespace carta {

// How a Zarr image's axes become CARTA's: spatial X, spatial Y, spectral, polarization.
//
// The library reports an image's axes in an order its schema profile chooses, each carrying a role,
// and asks a consumer not to assume the order. CARTA has one order of its own. Every place the
// backend crossed from one to the other -- the image's shape, a read request, the chunk shape a
// cursor is cut from, the storage layout the file-info panel shows -- used to do it itself, two of
// them by position. Here it is done once, by role.
//
// It is also where "can CARTA display this image" is decided, because the answer is the same
// question: an image whose axes do not map onto CARTA's four has no shape to report. Holding one of
// these is the proof that the image can be served; Of returns none, and says why, for one that
// cannot. The file list asks it before offering an image, and the image asks it before opening one,
// so the two cannot answer differently.
class CartaZarrAxes {
public:
    // The axes of an image CARTA can display, or none with `reason` saying why not. CARTA can
    // display an image with exactly one each of the spatial, spectral and polarization axes, no time
    // axis longer than one, no axis of any other kind, every length within casacore's, and the
    // direction, spectral and polarization coordinates it builds a coordinate system from.
    static std::optional<CartaZarrAxes> Of(const carta::zarr::ImageDescriptor& descriptor, std::string& reason);

    // The image's shape in CARTA's order.
    const casacore::IPosition& Shape() const {
        return _shape;
    }

    // A read of `section`, which is in CARTA's order, as a request in the image's own. A time axis
    // is read at its one plane.
    carta::zarr::ReadRequest Request(const casacore::Slicer& section) const;

    // One value per axis in the image's own order -- a chunk or shard shape, say -- in CARTA's. An
    // axis `values` does not reach reads as 1.
    std::vector<std::uint64_t> InCartaOrder(const std::vector<std::uint64_t>& values) const;

private:
    CartaZarrAxes() = default;

    // For each CARTA axis, where it is among the image's own.
    std::array<std::size_t, 4> _own_index{};
    std::optional<std::size_t> _time_index;
    std::size_t _rank = 0;
    casacore::IPosition _shape;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_
