/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_DATAEXPORTER_H_
#define CARTA_SRC_UTIL_DATAEXPORTER_H_

#include <iostream>

#include <carta-protobuf/export_data.pb.h>

namespace carta {
class DataExporter {
public:
    DataExporter(std::string top_level_folder);
    ~DataExporter() = default;

    void ExportData(const CARTA::ExportData& export_data_msg, CARTA::ExportDataAck& export_data_ack);

private:
    std::string _top_level_folder;
};
} // namespace carta

#endif // CARTA_SRC_UTIL_DATAEXPORTER_H_
