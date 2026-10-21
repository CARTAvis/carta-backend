/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_
#define CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_

#include <carta-zarr/descriptor.h>

#include <optional>
#include <string>
#include <vector>

#include <casacore/casa/aipstype.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Images/ImageBeamSet.h>
#include <casacore/images/Images/ImageInfo.h>

namespace carta {

// Builds the casacore coordinate system and beams of a Zarr image from carta-zarr's descriptors. Notes
// are returned rather than logged, so that the caller can name the file and show them in the file info.

// The file info value a note is about; a note about none is only logged
enum class ZarrNoteTopic { none, reference_pixels, pixel_increment, celestial_frame, frequency, beam };

// Something to know about a Zarr image's values, such as one that is approximate or was filled in: from
// carta-zarr's diagnostics or from the backend. `brief` is shown as the comment of the file info entry it
// is about, like a FITS header card comment; `detail` is the full sentence, for the log.
struct ZarrNote {
    ZarrNoteTopic topic = ZarrNoteTopic::none;
    std::string brief;
    std::string detail;
};

// carta-zarr's diagnostics as notes; an unknown code is kept with its name as `brief`
std::vector<ZarrNote> NotesFromDiagnostics(const std::vector<carta::zarr::Diagnostic>& diagnostics);

struct ZarrCoordinates {
    casacore::CoordinateSystem coordinates;
    std::vector<ZarrNote> notes;
};

// The coordinate system in CARTA's order (direction, spectral, Stokes) with the observation info, for an
// image CartaShape accepted. An image of the aperture plane has a linear coordinate named UU and VV in
// place of the direction. Throws casacore::AipsError for a frame, projection or Stokes label casacore does
// not know.
ZarrCoordinates MakeZarrCoordinateSystem(const carta::zarr::ImageDescriptor& descriptor);

struct ZarrBeams {
    // None if the table has no beam at the first time, or has beams for planes the image does not have
    std::optional<casacore::ImageBeamSet> beams;
    std::vector<ZarrNote> notes;
};

// The casacore image type a sky image's sub_type names, as FITS and HDF5 images take theirs from BTYPE. XRADIO
// spells casacore's type names without their spaces ("SpectralIndex" for "Spectral Index"). Intensity, casacore's
// default, for an image naming none or one casacore does not know.
casacore::ImageInfo::ImageTypes MakeZarrImageType(const std::string& sub_type);

// The beams at the first time, for an image of `channels` by `polarizations` planes. A table casacore
// refuses is ignored and noted.
ZarrBeams MakeZarrBeamSet(const std::vector<carta::zarr::Beam>& table, casacore::uInt channels, casacore::uInt polarizations);

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRCASACORE_H_
