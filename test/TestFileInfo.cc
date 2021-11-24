/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "BackendModel.h"
#include "CommonTestUtilities.h"
#include "Util/Message.h"

class FileInfoTest : public ::testing::Test {
public:
    void GetFileInfo(const std::string& filename, const std::string& hdu = "") {
        std::string directory = (TestRoot() / "data" / "images" / "mix").string();
        auto request = Message::FileInfoRequest(directory, filename, "");
        _dummy_backend->Receive(request);

        if (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::FILE_INFO_RESPONSE) {
                auto response = Message::DecodeMessage<CARTA::FileInfoResponse>(message);
                EXPECT_TRUE(response.success());
                EXPECT_EQ(response.file_info_extended_size(), 1);

                if (response.file_info_extended_size()) {
                    auto file_info_extended = response.file_info_extended();
                    EXPECT_NE(file_info_extended.find(hdu), file_info_extended.end());

                    if (file_info_extended.find(hdu) != file_info_extended.end()) {
                        int dimensions = 4;
                        int width = 6;
                        int height = 6;
                        int depth = 5;
                        int stokes = 1;
                        EXPECT_EQ(file_info_extended[hdu].dimensions(), dimensions);
                        EXPECT_EQ(file_info_extended[hdu].width(), width);
                        EXPECT_EQ(file_info_extended[hdu].height(), height);
                        EXPECT_EQ(file_info_extended[hdu].depth(), depth);
                        EXPECT_EQ(file_info_extended[hdu].stokes(), stokes);

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
                            }
                        }
                    }
                }
            }
        }
    }

private:
    void CheckHeaderEntry(const CARTA::HeaderEntry& header_entry, const std::string& value, const CARTA::EntryType& entry_type,
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

    std::unique_ptr<BackendModel> _dummy_backend = BackendModel::GetDummyBackend();
    std::pair<std::vector<char>, bool> _message_pair;
};

TEST_F(FileInfoTest, GetFileInfo) {
    GetFileInfo("M17_SWex_unit.image");
    GetFileInfo("M17_SWex_unit.miriad");
    GetFileInfo("M17_SWex_unit.hdf5", "0");
    GetFileInfo("M17_SWex_unit.fits", "0");
}
