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
// cannot. The file list asks it of a listed image's axes before offering it, and the image asks it
// again of its descriptor before opening, over the same axes, so the two cannot answer differently.
class CartaZarrAxes {
public:
    // The axes of an image CARTA can display, or none with `reason` saying why not. CARTA can
    // display an image with exactly one each of the spatial, spectral and polarization axes, no time
    // axis longer than one, no axis of any other kind, and every length above zero and within
    // casacore's.
    //
    // Asked of carta::zarr::ImageEntry::axes, which the listing carries, so that a dataset's images
    // can be sorted into those CARTA shows without opening any of them.
    static std::optional<CartaZarrAxes> Of(const std::vector<carta::zarr::AxisDescriptor>& axes, std::string& reason);
    // The same of an opened image's axes, and also that it has the direction, spectral and
    // polarization coordinates a coordinate system is built from.
    //
    // A listed image that passes the first cannot fail this. carta-zarr opens only a dataset whose
    // probe found a coordinate for every sky axis, so an openable image in one has all three; the
    // one exception, a spectral axis of no channels, the first refuses as an empty axis.
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
