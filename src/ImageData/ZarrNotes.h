/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRNOTES_H_
#define CARTA_SRC_IMAGEDATA_ZARRNOTES_H_

#include <carta-zarr/descriptor.h>

#include <string>
#include <vector>

#include <carta-protobuf/defs.pb.h>

namespace carta {

// What a value in the file-info panel is qualified by, which decides where a note about it goes.
// `none` is a note with no value to sit beside, which is logged rather than shown.
enum class ZarrNoteTopic { none, reference_pixels, pixel_increment, celestial_frame, frequency, beam };

// Something a reader should know about a Zarr image that opened: a value that is only approximately
// so, or was filled in rather than read. carta-zarr reports these as diagnostics on the image and
// the backend adds its own, such as a beam table that covers only some planes.
//
// Two tellings, because they go to two places. `brief` is a few words, shown as the comment on the
// entry the note qualifies -- "Frequency range = [...] / channels not evenly spaced" -- the way a
// FITS header card carries one. `detail` is the whole sentence, for the log.
struct ZarrNote {
    ZarrNoteTopic topic = ZarrNoteTopic::none;
    std::string brief;
    std::string detail;
};

// carta-zarr's diagnostics on an image as notes. A code this does not know keeps its own name as
// the brief telling, so a new one in the library is shown plainly rather than dropped.
std::vector<ZarrNote> NotesFromDiagnostics(const std::vector<carta::zarr::Diagnostic>& diagnostics);

// Write each note as the comment of the computed entries its topic qualifies, after any comment
// already there. Returns the notes that had nowhere to go: a topic of none, or an entry the panel
// did not show for this image.
std::vector<ZarrNote> AnnotateEntries(CARTA::FileInfoExtended& extended_info, const std::vector<ZarrNote>& notes);

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRNOTES_H_
