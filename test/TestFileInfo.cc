/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "Session.h"
#include "Util/Message.h"

static const std::string SAMPLE_FILES_PATH = (TestRoot() / "data" / "images" / "mix").string();

class FileInfoLoaderTest : public ::testing::Test {
public:
    static void CheckFileInfoLoader(const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu = "") {
        std::string fullname = SAMPLE_FILES_PATH + "/" + filename;
        CARTA::FileInfo file_info;
        file_info.set_name(filename);
        FileInfoLoader info_loader = FileInfoLoader(fullname);
        bool file_info_ok = info_loader.FillFileInfo(file_info);
        EXPECT_TRUE(file_info_ok);
        CheckFileInfo(file_info, filename, file_type, hdu);
    }

    static void CheckFileInfo(
        const CARTA::FileInfo& file_info, const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu) {
        EXPECT_EQ(file_info.name(), filename);
        EXPECT_EQ(file_info.type(), file_type);
        EXPECT_EQ(file_info.hdu_list_size(), 1); // since sample files only has a HDU
        if (file_info.hdu_list_size() && !hdu.empty()) {
            EXPECT_EQ(file_info.hdu_list(0), hdu);
        }
    }
};

class FileExtInfoLoaderTest : public ::testing::Test {
public:
    static void CheckFileExtInfoLoader(const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu = "") {
        std::string fullname = SAMPLE_FILES_PATH + "/" + filename;
        auto loader = std::shared_ptr<carta::FileLoader>(carta::FileLoader::GetLoader(fullname));
        FileExtInfoLoader ext_info_loader(loader);
        bool file_info_ok;
        std::string message;
        std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map;

        if (file_type == CARTA::FileType::FITS) {
            file_info_ok = ext_info_loader.FillFitsFileInfoMap(file_info_extended_map, fullname, message);
        } else {
            CARTA::FileInfoExtended file_info_ext;
            file_info_ok = ext_info_loader.FillFileExtInfo(file_info_ext, fullname, hdu, message);
            if (file_info_ok) {
                file_info_extended_map[hdu] = file_info_ext;
            }
        }

        CheckFileInfoExtended(file_info_extended_map, filename, hdu);
    }

    static void CheckFileInfoExtended(
        std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map, const std::string& filename, const std::string& hdu) {
        EXPECT_EQ(file_info_extended_map[hdu].dimensions(), 4);
        EXPECT_EQ(file_info_extended_map[hdu].width(), 6);
        EXPECT_EQ(file_info_extended_map[hdu].height(), 6);
        EXPECT_EQ(file_info_extended_map[hdu].depth(), 5);
        EXPECT_EQ(file_info_extended_map[hdu].stokes(), 1);

        for (auto header_entry : file_info_extended_map[hdu].header_entries()) {
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

        for (auto computed_entries : file_info_extended_map[hdu].computed_entries()) {
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

class SessionFileInfoTest : public ::testing::Test {
public:
    static void CheckFileInfoResponse(
        const std::string& filename, const CARTA::FileType& file_type, const std::string& hdu, const CARTA::FileInfoResponse& response) {
        EXPECT_TRUE(response.success());

        auto file_info = response.file_info();
        if (file_type == CARTA::HDF5) {
            FileInfoLoaderTest::CheckFileInfo(file_info, filename, file_type, hdu);
        } else {
            FileInfoLoaderTest::CheckFileInfo(file_info, filename, file_type, "");
        }

        auto file_info_extended = response.file_info_extended();
        EXPECT_NE(file_info_extended.find(hdu), file_info_extended.end());

        if (file_info_extended.find(hdu) != file_info_extended.end()) {
            // convert protobuf map to std map
            std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map = {file_info_extended.begin(), file_info_extended.end()};
            FileExtInfoLoaderTest::CheckFileInfoExtended(file_info_extended_map, filename, hdu);
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

        SessionFileInfoTest::CheckFileInfoResponse(filename, file_type, hdu, response);
    }
};

TEST_F(FileInfoLoaderTest, CasaFile) {
    CheckFileInfoLoader("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileInfoLoaderTest, FitsFile) {
    CheckFileInfoLoader("M17_SWex_unit.fits", CARTA::FileType::FITS);
}

TEST_F(FileInfoLoaderTest, Hdf5File) {
    CheckFileInfoLoader("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0"); // FileInfoLoader only get HDU list for a HDF5 file
}

TEST_F(FileInfoLoaderTest, MiriadFile) {
    CheckFileInfoLoader("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}

TEST_F(FileExtInfoLoaderTest, CasaFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileExtInfoLoaderTest, FitsFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.fits", CARTA::FileType::FITS, "0");
}

TEST_F(FileExtInfoLoaderTest, Hdf5File) {
    CheckFileExtInfoLoader("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0");
}

TEST_F(FileExtInfoLoaderTest, MiriadFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}

TEST_F(SessionFileInfoTest, CasaFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(SessionFileInfoTest, FitsFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.fits", CARTA::FileType::FITS, "0");
}

TEST_F(SessionFileInfoTest, Hdf5File) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0");
}

TEST_F(SessionFileInfoTest, MiriadFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}
