/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
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

// Builds the casacore coordinate system and beams of a Zarr image from carta-zarr's descriptors. Notes
// are returned rather than logged, so that the caller can name the file and show them in the file info.

struct ZarrCoordinates {
    casacore::CoordinateSystem coordinates;
    std::vector<ZarrNote> notes;
};

// The coordinate system in CARTA's order (direction, spectral, Stokes) with the observation info, for an
// image CartaZarrAxes accepted. Throws casacore::AipsError for a frame, projection or Stokes label casacore
// does not know.
ZarrCoordinates MakeZarrCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor);

struct ZarrBeams {
    // None if the table has no beam at the first time, or has beams for planes the image does not have
    std::optional<casacore::ImageBeamSet> beams;
    std::vector<ZarrNote> notes;
};

// The beams at the first time, for an image of `channels` by `polarizations` planes. A table casacore
// refuses is ignored and noted.
ZarrBeams MakeZarrBeamSet(const std::vector<carta::zarr::Beam>& table, casacore::uInt channels, casacore::uInt polarizations);

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_
