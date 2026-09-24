/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrNotes.h"

#include <string_view>

namespace carta {
namespace {

// The value a coordinate diagnostic on `axis` qualifies: the spectrum's range for the frequency
// axis, `for_direction` for l or m, and nothing for an axis the panel shows no value of.
ZarrNoteTopic TopicFor(const std::string& axis, ZarrNoteTopic for_direction) {
    if (axis == "frequency") {
        return ZarrNoteTopic::frequency;
    }
    if (axis == "l" || axis == "m") {
        return for_direction;
    }
    return ZarrNoteTopic::none;
}

bool StartsWith(const std::string& text, std::string_view prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

// Whether the computed entry named `name` shows the value a note on `topic` qualifies. The names are
// FileExtInfoLoader's own; a beam is one entry per Stokes when the planes differ, and each gets it.
bool Shows(ZarrNoteTopic topic, const std::string& name) {
    switch (topic) {
        case ZarrNoteTopic::reference_pixels:
            return name == "Image reference pixels";
        case ZarrNoteTopic::pixel_increment:
            return name == "Pixel increment";
        case ZarrNoteTopic::celestial_frame:
            return name == "Celestial frame";
        case ZarrNoteTopic::frequency:
            return name == "Frequency range" || name == "Frequency";
        case ZarrNoteTopic::beam:
            return StartsWith(name, "Restoring beam") || StartsWith(name, "Median area beam");
        default:
            return false;
    }
}

}  // namespace

std::vector<ZarrNote> NotesFromDiagnostics(const std::vector<carta::zarr::Diagnostic>& diagnostics) {
    std::vector<ZarrNote> notes;
    for (const auto& diagnostic : diagnostics) {
        ZarrNote note;
        note.detail = diagnostic.message;
        const auto& axis = diagnostic.node_path;
        switch (diagnostic.code) {
            case carta::zarr::DiagnosticCode::nonuniform_axis:
                note.brief = axis == "frequency" ? "channels not evenly spaced" : axis + " samples not evenly spaced";
                note.topic = TopicFor(axis, ZarrNoteTopic::pixel_increment);
                break;
            case carta::zarr::DiagnosticCode::inexact_reference_pixel:
                note.brief = axis + " reference pixel extrapolated";
                note.topic = TopicFor(axis, ZarrNoteTopic::reference_pixels);
                break;
            case carta::zarr::DiagnosticCode::degenerate_axis:
                note.brief = axis + " has no increment";
                note.topic = TopicFor(axis, ZarrNoteTopic::pixel_increment);
                break;
            default:
                // Nothing in the panel it qualifies, or a code newer than this: said by name, in the log.
                note.brief = carta::zarr::DiagnosticCodeName(diagnostic.code);
                break;
        }
        notes.push_back(std::move(note));
    }
    return notes;
}

std::vector<ZarrNote> AnnotateEntries(CARTA::FileInfoExtended& extended_info, const std::vector<ZarrNote>& notes) {
    std::vector<ZarrNote> unplaced;
    for (const auto& note : notes) {
        bool placed = false;
        for (auto& entry : *extended_info.mutable_computed_entries()) {
            if (!Shows(note.topic, entry.name())) {
                continue;
            }
            placed = true;
            const auto& comment = entry.comment();
            if (comment.find(note.brief) != std::string::npos) {
                continue;  // said already, by another note of the same kind
            }
            entry.set_comment(comment.empty() ? note.brief : comment + "; " + note.brief);
        }
        if (!placed) {
            unplaced.push_back(note);
        }
    }
    return unplaced;
}

}  // namespace carta
