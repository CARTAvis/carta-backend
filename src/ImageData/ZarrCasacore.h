/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_
#define CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_

#include <carta-zarr/descriptor.h>

#include "ZarrNotes.h"

#include <optional>
#include <string>
#include <vector>

#include <casacore/casa/aipstype.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Images/ImageBeamSet.h>

namespace carta {

// What casacore needs to be told about a Zarr image, made from what carta-zarr describes.
//
// The descriptors are built to feed casacore directly rather than through FITS header cards (the
// library's ADR 0002), and this is where they do. Everything here is a rule about casacore rather
// than about Zarr -- a reference pixel counted from one becomes one counted from zero, an empty frame
// is J2000 or LSRK, a table that gives every plane the same beam is a single beam -- and each used
// to be reachable only by opening a store on disk. Taking descriptors and returning casacore
// objects, it is checked from values written in a test.

// The coordinate system of an image, and what there was to say about how it was made.
struct ZarrCoordinates {
    casacore::CoordinateSystem coordinates;
    // What a reader should be told, such as an equinox the direction frame cannot hold. Said here
    // rather than logged so that the caller can say which file it was, and show it where it belongs.
    std::vector<ZarrNote> notes;
};

// The coordinate system of an image in CARTA's order -- direction, spectral, Stokes -- with the
// observation it carries. Throws casacore::AipsError for what casacore cannot express at all: a
// frame, projection, Stokes label or time scale it does not know. What it can express only nearly,
// it does, and notes. Only asked of an image CartaZarrAxes accepted, which is what says the three
// coordinates are there.
ZarrCoordinates MakeZarrCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor);

// The restoring beams of an image, and what there was to say about the table they came from.
struct ZarrBeams {
    // None when the table has nothing to put on this image: no beam at the first time, or beams
    // for planes the image does not have.
    std::optional<casacore::ImageBeamSet> beams;
    // What a reader should be told, such as a table that covers only some planes. Said here rather
    // than logged so that the caller can say which file it was, and show it where it belongs.
    std::vector<ZarrNote> notes;
};

// The beams at the first time, on an image of `channels` by `polarizations` planes. A table that
// gives every plane exactly the same beam is a single beam, because hasMultipleBeams() decides
// whether a consumer convolves the cube to a common one first. A table with a beam casacore refuses
// is ignored, and noted, rather than thrown.
ZarrBeams MakeZarrBeamSet(const std::vector<carta::zarr::Beam>& table, casacore::uInt channels, casacore::uInt polarizations);

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_
