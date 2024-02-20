/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <carta-protobuf/export_data.pb.h>

#include "CommonTestUtilities.h"
#include "Timer/Timer.h"
#include "Util/DataExporter.h"

using namespace carta;

static const std::string TEST_PATH = (TestRoot() / "data").string();

class DataExporterTest : public ::testing::Test {
public:
    static std::string GetCurrentTime() {
        time_t rawtime;
        struct tm* timeinfo;
        char buffer[80];

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", timeinfo);
        std::string result(buffer);
        return result;
    }

    static void FillExportData(CARTA::ExportData& export_data, std::string directory, std::string name, int line_num) {
        export_data.set_directory(directory);
        export_data.set_name(name);

        export_data.add_comments("# SgrB2-N.spw0.line.fits X profile");
        export_data.add_comments("# xLabel: X coordinate");
        export_data.add_comments("# yLabel: Value (Jy/beam)");
        export_data.add_comments("# Point (pixel) [291.897661pix, 348.505848pix]");
        export_data.add_comments("# Point (wcs:FK5) [17:47:18.6118295045, -28:21:41.2777674663]");
        export_data.add_comments("# x     y");

        for (int i = 0; i < line_num; ++i) {
            double x_val = ((double)rand() * 2.0 / RAND_MAX) - 1.0;
            double y_val = ((double)rand() * 2.0 / RAND_MAX) - 1.0;
            std::string x_str = fmt::format("{:.9f}", x_val);
            std::string y_str = fmt::format("{:.9f}", y_val);
            std::string xy_str = fmt::format("{}    {}", x_str, y_str);
            export_data.add_data(xy_str);
        }
    }

    static void TestExportDataMsg(std::string directory, std::string name, int line_num, bool expect_success) {
        CARTA::ExportData export_data_msg;
        FillExportData(export_data_msg, directory, name, line_num);

        DataExporter data_exporter = DataExporter(TEST_PATH);
        CARTA::ExportDataAck export_data_ack;

        Timer t;
        data_exporter.ExportData(export_data_msg, export_data_ack);
        std::cout << fmt::format("Elapsed time to export the data {:.3f} ms, data line number {}.\n", t.Elapsed().ms(), line_num);

        if (expect_success) {
            EXPECT_TRUE(export_data_ack.success());
        } else {
            EXPECT_FALSE(export_data_ack.success());
        }

        if (export_data_ack.success()) {
            fs::path filename = fs::path(TEST_PATH) / fs::path(directory) / fs::path(name);
            EXPECT_TRUE(fs::exists(filename));

            std::ifstream file(filename);
            if (file.is_open()) {
                string line_str;
                size_t i = 0;
                size_t i_comment = 0;
                size_t i_data = 0;
                auto comment_line_num = export_data_msg.comments_size();
                auto data_line_num = export_data_msg.data_size();
                while (getline(file, line_str)) {
                    std::string input_str;
                    if (i_comment < comment_line_num) {
                        input_str = export_data_msg.comments(i_comment);
                        ++i_comment;
                    } else if (i_data < data_line_num) {
                        input_str = export_data_msg.data(i_data);
                        ++i_data;
                    }
                    EXPECT_EQ(input_str, line_str);
                    ++i;
                }
                file.close();
            }

            if (name != "test-image-profiles.tsv") {
                fs::remove(filename.string());
                EXPECT_FALSE(fs::exists(filename));
            }
        }
    }
};

TEST_F(DataExporterTest, PathNotExist) {
    std::string filename = "profiles-" + GetCurrentTime() + ".tsv";
    TestExportDataMsg("path-not-exist", filename, 100, false);
}

TEST_F(DataExporterTest, InvalidPath) {
    std::string filename = "profiles-" + GetCurrentTime() + ".tsv";
    TestExportDataMsg("..", filename, 100, false);
    TestExportDataMsg("../profiles", filename, 100, false);
    TestExportDataMsg("~", filename, 100, false);
    TestExportDataMsg("~/", filename, 100, false);
}

TEST_F(DataExporterTest, OverwriteFile) {
    TestExportDataMsg("profiles", "test-image-profiles.tsv", 100, true);
}

TEST_F(DataExporterTest, CreateNewFile) {
    std::string filename = "profiles-" + GetCurrentTime() + ".tsv";
    TestExportDataMsg("profiles", filename, 100, true);
}
