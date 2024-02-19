/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DataExporter.h"

#include "Logger/Logger.h"

#include <filesystem>
#include <fstream>

using namespace carta;

DataExporter::DataExporter(std::string top_level_folder) : _top_level_folder(top_level_folder) {}

void DataExporter::ExportData(const CARTA::ExportData& export_data_msg, CARTA::ExportDataAck& export_data_ack) {
    auto handle_error = [&](std::string err) {
        spdlog::error(err);
        export_data_ack.set_success(false);
        export_data_ack.set_message(err);
    };

    std::string directory_str = export_data_msg.directory();
    if (directory_str.find("..") != std::string::npos) {
        handle_error("Invalid request directory!");
        return;
    }

    fs::path output_filename;
    try {
        fs::path directory = fs::path(_top_level_folder) / fs::path(directory_str);
        fs::path abs_directory = fs::absolute(directory);
        output_filename = abs_directory / fs::path(export_data_msg.name());
    } catch (std::filesystem::filesystem_error const& ex) {
        handle_error(ex.what());
        return;
    }

    if (fs::exists(output_filename)) {
        spdlog::warn("File {} exists! Overwrite it.", output_filename.string());
        fs::remove(output_filename);
    }

    std::ofstream ofs;
    ofs.open(output_filename);
    if (!ofs.is_open()) {
        handle_error("Fail to open the exported data file!");
        return;
    }

    for (int i = 0; i < export_data_msg.comments_size(); ++i) {
        ofs << export_data_msg.comments(i) << "\n";
    }
    for (int i = 0; i < export_data_msg.data_size(); ++i) {
        ofs << export_data_msg.data(i) << "\n";
    }
    ofs.close();
    export_data_ack.set_success(true);
}
