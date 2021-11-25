/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "Session.h"
#include "Util/Message.h"

static const std::string SAMPLE_FILES_PATH = (TestRoot() / "data" / "images" / "mix").string();

class FileInfoTest : public ::testing::Test {
public:
    static void CheckFileInfoResponse(
        const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu, const CARTA::FileInfoResponse& response) {
        EXPECT_TRUE(response.success());

        auto file_info = response.file_info();
        EXPECT_EQ(file_info.type(), file_type);
        EXPECT_EQ(file_info.hdu_list_size(), 1);

        auto file_info_extended = response.file_info_extended();
        EXPECT_NE(file_info_extended.find(hdu), file_info_extended.end());

        if (file_info_extended.find(hdu) != file_info_extended.end()) {
            EXPECT_EQ(file_info_extended[hdu].dimensions(), 4);
            EXPECT_EQ(file_info_extended[hdu].width(), 6);
            EXPECT_EQ(file_info_extended[hdu].height(), 6);
            EXPECT_EQ(file_info_extended[hdu].depth(), 5);
            EXPECT_EQ(file_info_extended[hdu].stokes(), 1);

            for (auto header_entry : file_info_extended[hdu].header_entries()) {
                if (header_entry.name() == "SIMPLE") {
                    CheckHeaderEntry(header_entry, "T", CARTA::EntryType::STRING, 0, "Standard FITS");
                } else if (header_entry.name() == "BITPIX") {
                    CheckHeaderEntry(header_entry, "-32", CARTA::EntryType::INT, -32);
                } else if (header_entry.name() == "NAXIS") {
                    CheckHeaderEntry(header_entry, "4", CARTA::EntryType::INT, 4);
                } else if (header_entry.name() == "NAXIS1") {
                    CheckHeaderEntry(header_entry, "6", CARTA::EntryType::INT, 6);
                } else if (header_entry.name() == "NAXIS2") {
                    CheckHeaderEntry(header_entry, "6", CARTA::EntryType::INT, 6);
                } else if (header_entry.name() == "NAXIS3") {
                    CheckHeaderEntry(header_entry, "5", CARTA::EntryType::INT, 5);
                } else if (header_entry.name() == "NAXIS4") {
                    CheckHeaderEntry(header_entry, "1", CARTA::EntryType::INT, 1);
                } else if (header_entry.name() == "EXTEND") {
                    CheckHeaderEntry(header_entry, "T", CARTA::EntryType::STRING, 0);
                } else if (header_entry.name() == "BSCALE") {
                    CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1, "PHYSICAL = PIXEL*BSCALE + BZERO");
                } else if (header_entry.name() == "PC1_1") {
                    CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
                } else if (header_entry.name() == "PC2_2") {
                    CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
                } else if (header_entry.name() == "PC3_3") {
                    CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
                } else if (header_entry.name() == "PC4_4") {
                    CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
                }
            }

            for (auto computed_entries : file_info_extended[hdu].computed_entries()) {
                if (computed_entries.name() == "Name") {
                    CheckHeaderEntry(computed_entries, filename, CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "HDU") {
                    CheckHeaderEntry(computed_entries, "0", CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "Shape") {
                    CheckHeaderEntry(computed_entries, "[6, 6, 5, 1]", CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "Number of channels") {
                    CheckHeaderEntry(computed_entries, "5", CARTA::EntryType::INT, 5);
                } else if (computed_entries.name() == "Number of polarizations") {
                    CheckHeaderEntry(computed_entries, "1", CARTA::EntryType::INT, 1);
                } else if (computed_entries.name() == "Coordinate type") {
                    CheckHeaderEntry(computed_entries, "Right Ascension, Declination", CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "Velocity definition") {
                    CheckHeaderEntry(computed_entries, "RADIO", CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "Pixel unit") {
                    CheckHeaderEntry(computed_entries, "Jy/beam", CARTA::EntryType::STRING);
                } else if (computed_entries.name() == "Pixel increment") {
                    CheckHeaderEntry(computed_entries, "-0.4\", 0.4\"", CARTA::EntryType::STRING);
                }
            }
        }
    }

    static void CheckHeaderEntry(const CARTA::HeaderEntry& header_entry, const std::string& value, const CARTA::EntryType& entry_type,
        double numeric_value = std::numeric_limits<double>::quiet_NaN(), const std::string& comment = "") {
        EXPECT_EQ(header_entry.value(), value);
        EXPECT_EQ(header_entry.entry_type(), entry_type);
        if (!isnan(numeric_value)) {
            EXPECT_DOUBLE_EQ(header_entry.numeric_value(), numeric_value);
        }
        if (!comment.empty()) {
            EXPECT_EQ(header_entry.value(), value);
        }
    }
};

class TestSession : public Session {
public:
    TestSession() : Session(nullptr, nullptr, 0, "", "/", "", nullptr, -1, false) {}

    void TestFileInfo(const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu = "") {
        auto request = Message::FileInfoRequest(SAMPLE_FILES_PATH, filename, "");
        CARTA::FileInfoResponse response;
        auto& file_info = *response.mutable_file_info();
        std::map<std::string, CARTA::FileInfoExtended> extended_info_map;
        string message;
        bool success = FillExtendedFileInfo(extended_info_map, file_info, request.directory(), request.file(), request.hdu(), message);

        if (success) {
            *response.mutable_file_info_extended() = {extended_info_map.begin(), extended_info_map.end()};
        }
        response.set_success(success);

        FileInfoTest::CheckFileInfoResponse(filename, file_type, hdu, response);
    }
};

TEST_F(FileInfoTest, CASAFileInfo) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileInfoTest, FitsFileInfo) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.fits", CARTA::FileType::FITS, "0");
}

TEST_F(FileInfoTest, Hdf5FileInfo) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0");
}

TEST_F(FileInfoTest, MiriadFileInfo) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}
