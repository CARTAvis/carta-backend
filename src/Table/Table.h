/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef VOTABLE_TEST__TABLE_H_
#define VOTABLE_TEST__TABLE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "Columns.h"
#include "TableView.h"

#define MAX_HEADER_SIZE (64 * 1024)

namespace carta {

class TableView;

struct TableParam {
    std::string name;
    std::string description;
    std::string value;
};

class Table {
public:
    Table(const std::string& filename, bool header_only = false);
    bool IsValid() const;
    std::string ParseError() const;
    const Column* GetColumnByName(const std::string& name) const;
    const Column* GetColumnById(const std::string& id) const;
    size_t NumColumns() const;
    size_t NumRows() const;
    size_t AvailableRows() const;
    CARTA::CatalogFileType Type() const;
    const std::string& Description() const;
    const std::string Parameters() const;
    const CARTA::Coosys& Coosys() const;
    TableView View() const;

    const Column* operator[](size_t i) const;
    const Column* operator[](const std::string& name_or_id) const;

protected:
    bool ConstructFromXML(bool header_only = false);
    bool PopulateCoosys(const pugi::xml_node& votable);
    bool PopulateParams(const pugi::xml_node& table);
    bool PopulateFields(const pugi::xml_node& table);
    bool PopulateRows(const pugi::xml_node& table);

    bool ConstructFromFITS(bool header_only = false);

    bool _valid;
    CARTA::CatalogFileType _file_type;
    CARTA::Coosys _coosys;
    int64_t _num_rows;
    int64_t _available_rows;
    std::string _parse_error_message;
    std::string _filename;
    std::string _description;
    std::vector<TableParam> _params;
    std::vector<std::unique_ptr<Column>> _columns;
    std::unordered_map<std::string, Column*> _column_name_map;
    std::unordered_map<std::string, Column*> _column_id_map;
    static std::string GetHeader(const std::string& filename);
};
} // namespace carta
#endif // VOTABLE_TEST__TABLE_H_
