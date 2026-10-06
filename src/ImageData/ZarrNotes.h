/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRNOTES_H_
#define CARTA_SRC_IMAGEDATA_ZARRNOTES_H_

#include <carta-zarr/descriptor.h>

#include <string>
#include <vector>

#include <carta-protobuf/defs.pb.h>

namespace carta {

// The file info value a note is about; a note about none is logged rather than shown
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

// Appends each note to the comment of the computed entries it is about. Returns the notes with no such entry.
std::vector<ZarrNote> AnnotateEntries(CARTA::FileInfoExtended& extended_info, const std::vector<ZarrNote>& notes);

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRNOTES_H_
