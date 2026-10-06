/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "FileList/FileExtInfoLoader.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrNotes.h"

using carta::AnnotateEntries;
using carta::NotesFromDiagnostics;
using carta::ZarrNote;
using carta::ZarrNoteTopic;
using carta::zarr::DiagnosticCode;

namespace {

carta::zarr::Diagnostic Said(DiagnosticCode code, const std::string& axis) {
    return carta::zarr::Diagnostic{code, "the whole sentence about " + axis, axis};
}

CARTA::FileInfoExtended PanelWith(const std::vector<std::string>& names) {
    CARTA::FileInfoExtended info;
    for (const auto& name : names) {
        info.add_computed_entries()->set_name(name);
    }
    return info;
}

const CARTA::HeaderEntry& Entry(const CARTA::FileInfoExtended& info, const std::string& name) {
    for (const auto& entry : info.computed_entries()) {
        if (entry.name() == name) {
            return entry;
        }
    }
    throw std::runtime_error("no entry " + name);
}

} // namespace

TEST(ZarrNotes, ACoordinateDiagnosticQualifiesTheValueItIsAbout) {
    const auto notes =
        NotesFromDiagnostics({Said(DiagnosticCode::nonuniform_axis, "frequency"), Said(DiagnosticCode::inexact_reference_pixel, "l"),
            Said(DiagnosticCode::nonuniform_axis, "m"), Said(DiagnosticCode::degenerate_axis, "frequency")});
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0].topic, ZarrNoteTopic::frequency);
    EXPECT_EQ(notes[0].brief, "channels not evenly spaced");
    EXPECT_EQ(notes[0].detail, "the whole sentence about frequency") << "the library's sentence is kept for the log";
    EXPECT_EQ(notes[1].topic, ZarrNoteTopic::reference_pixels);
    EXPECT_EQ(notes[1].brief, "l reference pixel extrapolated");
    EXPECT_EQ(notes[2].topic, ZarrNoteTopic::pixel_increment);
    EXPECT_EQ(notes[2].brief, "m samples not evenly spaced");
    EXPECT_EQ(notes[3].topic, ZarrNoteTopic::frequency);
}

// A code not about a shown value, or an unknown one, keeps its name and is left for the log
TEST(ZarrNotes, ACodeWithNoValueToQualifyIsSaidByName) {
    const auto notes = NotesFromDiagnostics({Said(DiagnosticCode::ambiguous_pixel_mask, "SKY")});
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].topic, ZarrNoteTopic::none);
    EXPECT_EQ(notes[0].brief, "ambiguous_pixel_mask");
}

TEST(ZarrNotes, ANoteIsTheCommentOfTheEntryItQualifies) {
    auto info = PanelWith({"Name", "Celestial frame", "Frequency range", "Pixel increment"});
    const auto unplaced = AnnotateEntries(info, {ZarrNote{ZarrNoteTopic::frequency, "channels not evenly spaced", ""},
                                                    ZarrNote{ZarrNoteTopic::celestial_frame, "equinox 1975 read as J2000", ""}});
    EXPECT_TRUE(unplaced.empty());
    EXPECT_EQ(Entry(info, "Frequency range").comment(), "channels not evenly spaced");
    EXPECT_EQ(Entry(info, "Celestial frame").comment(), "equinox 1975 read as J2000");
    EXPECT_TRUE(Entry(info, "Name").comment().empty()) << "an entry no note is about is left alone";
    EXPECT_TRUE(Entry(info, "Pixel increment").comment().empty());
}

// An existing comment stays, and two notes on one value are both added once
TEST(ZarrNotes, NotesOnOneValueAreJoinedAfterWhatWasThere) {
    auto info = PanelWith({"Pixel increment"});
    info.mutable_computed_entries(0)->set_comment("from the header");
    AnnotateEntries(info, {ZarrNote{ZarrNoteTopic::pixel_increment, "l samples not evenly spaced", ""},
                              ZarrNote{ZarrNoteTopic::pixel_increment, "m samples not evenly spaced", ""},
                              ZarrNote{ZarrNoteTopic::pixel_increment, "m samples not evenly spaced", ""}});
    EXPECT_EQ(Entry(info, "Pixel increment").comment(), "from the header; l samples not evenly spaced; m samples not evenly spaced");
}

// With per-plane beams there is one beam entry per Stokes, and each gets the note
TEST(ZarrNotes, ABeamNoteGoesOnEveryBeamEntry) {
    auto info = PanelWith({"Median area beam (Stokes I)", "Median area beam (Stokes Q)"});
    AnnotateEntries(info, {ZarrNote{ZarrNoteTopic::beam, "table covers 5 of 6 planes", ""}});
    EXPECT_EQ(Entry(info, "Median area beam (Stokes I)").comment(), "table covers 5 of 6 planes");
    EXPECT_EQ(Entry(info, "Median area beam (Stokes Q)").comment(), "table covers 5 of 6 planes");
}

TEST(ZarrNotes, WhatHasNowhereToGoIsHandedBack) {
    auto info = PanelWith({"Name"});
    const auto unplaced = AnnotateEntries(
        info, {ZarrNote{ZarrNoteTopic::none, "ambiguous_pixel_mask", ""}, ZarrNote{ZarrNoteTopic::beam, "table covers 5 of 6 planes", ""}});
    ASSERT_EQ(unplaced.size(), 2u) << "no topic, and a topic whose entry the panel did not show";
}

// The fixture's channels are unevenly spaced, which shows in the file info
TEST(ZarrNotes, TheFileInfoSaysWhatTheLibrarySaid) {
    const std::filesystem::path fixture{ZARR_XRADIO_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio fixture not found at " << fixture;
    }
    auto loader = std::shared_ptr<carta::FileLoader>(carta::FileLoader::GetLoader(fixture.string()));
    ASSERT_NE(loader, nullptr);
    carta::FileExtInfoLoader ext_info_loader(loader);
    CARTA::FileInfoExtended info;
    std::string message;
    ASSERT_TRUE(ext_info_loader.FillFileExtInfo(info, fixture.string(), "SKY", message)) << message;
    EXPECT_NE(Entry(info, "Frequency range").comment().find("channels not evenly spaced"), std::string::npos)
        << "comment was '" << Entry(info, "Frequency range").comment() << "'";
}
