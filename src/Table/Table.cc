/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Table.h"

#include <fstream>
#include <iostream>

#include <fitsio.h>

#include "../Logger/Logger.h"

#include "DataColumn.tcc"
#include "ThreadingManager/ThreadingManager.h"
#include "Util/File.h"
#include "Util/FileSystem.h"

namespace carta {

Table::Table(const string& filename, bool header_only) : _valid(false), _filename(filename), _num_rows(0), _available_rows(0) {
    fs::path file_path(filename);
    std::error_code error_code;
    if (!fs::exists(file_path, error_code)) {
        _parse_error_message = "File does not exist or cannot be read!";
        return;
    }

    auto magic_number = GetMagicNumber(filename);
    if (magic_number == FITS_MAGIC_NUMBER) {
        _valid = ConstructFromFITS(header_only);
        _file_type = CARTA::FITSTable;
    } else if (magic_number == XML_MAGIC_NUMBER) {
        _valid = ConstructFromXML(header_only);
        _file_type = CARTA::VOTable;
    } else {
    }
}

string Table::GetHeader(const string& filename) {
    ifstream in(filename);
    string header_string;

    if (!in.good()) {
        return header_string;
    }

    // Measure entire file size to ensure we don't read past EOF
    in.seekg(0, ios_base::end);
    size_t header_size = min(size_t(in.tellg()), size_t(MAX_HEADER_SIZE));
    header_string.resize(header_size);
    in.seekg(0, ios_base::beg);
    in.read(&header_string[0], header_string.size());
    in.close();

    // Resize to exclude the start of the <DATA> tag
    auto data_index = header_string.find("<DATA>");
    if (data_index != string::npos) {
        header_string.resize(data_index);
    }
    return header_string;
}

bool Table::ConstructFromXML(bool header_only) {
    pugi::xml_document doc;

    // read the first 64K only and construct a header from this
    if (header_only) {
        string header_string = GetHeader(_filename);
        auto result = doc.load_string(header_string.c_str(), pugi::parse_default | pugi::parse_fragment);
        if (!result && result.status != pugi::status_end_element_mismatch) {
            spdlog::error(result.description());
            return false;
        }
    } else {
        auto result = doc.load_file(_filename.c_str(), pugi::parse_default | pugi::parse_embed_pcdata);
        if (!result) {
            spdlog::error(result.description());
            return false;
        }
    }
    auto votable = doc.child("VOTABLE");

    if (!votable) {
        _parse_error_message = "Missing XML element VOTABLE!";
        return false;
    }

    PopulateCoosys(votable);

    auto resource = votable.child("RESOURCE");
    if (!resource) {
        _parse_error_message = "Missing XML element RESOURCE!";
        return false;
    }

    auto table_node = resource.child("TABLE");
    if (!table_node) {
        _parse_error_message = "Missing XML element TABLE!";
        return false;
    }

    auto description = table_node.child("DESCRIPTION");
    if (description) {
        _description = description.text().as_string();
    }

    PopulateParams(table_node);

    if (!PopulateFields(table_node)) {
        _parse_error_message = "Cannot parse table headers!";
        return false;
    }

    // Once fields are populated, stop parsing
    if (header_only) {
        auto num_rows_attribute = table_node.attribute("nrows");
        if (num_rows_attribute) {
            _available_rows = num_rows_attribute.as_int();
        }
        return true;
    }

    if (!PopulateRows(table_node)) {
        _parse_error_message = "Cannot parse table data!";
        return false;
    }

    return true;
}

bool Table::PopulateCoosys(const pugi::xml_node& votable) {
    if (!votable) {
        return false;
    }

    auto resource = votable.child("RESOURCE");
    if (!resource) {
        return false;
    }

    pugi::xml_node coosys_node;
    if (votable.child("COOSYS")) {
        coosys_node = votable.child("COOSYS");
    } else if (votable.child("DEFINITIONS").child("COOSYS")) {
        coosys_node = votable.child("DEFINITIONS").child("COOSYS");
    } else {
        coosys_node = resource.child("COOSYS");
    }

    if (coosys_node) {
        auto epoch = coosys_node.attribute("epoch").value();
        auto equinox = coosys_node.attribute("equinox").value();
        auto system = coosys_node.attribute("system").value();
        if (epoch) {
            _coosys.set_epoch(epoch);
        }
        if (equinox) {
            _coosys.set_equinox(equinox);
        }
        if (system) {
            _coosys.set_system(system);
        }
        return true;
    } else {
        return false;
    }
}

bool Table::PopulateParams(const pugi::xml_node& table) {
    if (!table) {
        return false;
    }

    for (auto& field : table.children("PARAM")) {
        string name = field.attribute("name").as_string();
        string value = field.attribute("value").as_string();
        auto description_node = field.child("DESCRIPTION");
        string description;
        if (description_node) {
            description = description_node.child_value();
        }
        _params.emplace_back(TableParam({name, description, value}));
    }

    return true;
}

bool Table::PopulateFields(const pugi::xml_node& table) {
    if (!table) {
        return false;
    }

    for (auto& field : table.children("FIELD")) {
        auto& column = _columns.emplace_back(Column::FromField(field));
        if (!column->name.empty()) {
            _column_name_map[column->name] = column.get();
        }
        if (!column->id.empty()) {
            _column_id_map[column->id] = column.get();
        }
    }

    return !_columns.empty();
}

bool Table::PopulateRows(const pugi::xml_node& table) {
    auto data = table.child("DATA");
    auto table_data = data.child("TABLEDATA");
    if (!table_data) {
        return false;
    }

    // VOTable standard specifies TABLEDATA element contains only TR children, which contain only TD children
    auto row_nodes = table_data.children();

    std::vector<pugi::xml_node> rows;
    for (auto& row : row_nodes) {
        rows.push_back(row);
    }

    _num_rows = rows.size();
    for (auto& column : _columns) {
        column->Resize(_num_rows);
    }

    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for schedule(static) default(none) shared(_num_rows, rows)
    for (auto i = 0; i < _num_rows; i++) {
        auto& row = rows[i];
        auto column_iterator = _columns.begin();
        auto column_nodes = row.children();
        for (auto& td : column_nodes) {
            if (column_iterator == _columns.end()) {
                break;
            }
            (*column_iterator)->SetFromText(td.text(), i);
            column_iterator++;
        }

        // Fill remaining / missing columns
        while (column_iterator != _columns.end()) {
            (*column_iterator)->SetEmpty(i);
            column_iterator++;
        }
    }

    return true;
}

bool Table::ConstructFromFITS(bool header_only) {
    fitsfile* file_ptr = nullptr;
    int status = 0;
    // Attempt to open the first table HDU. status = 0 means no error
    if (fits_open_table(&file_ptr, _filename.c_str(), READONLY, &status)) {
        _parse_error_message = "File does not contain a FITS table!";
        return false;
    }

    char ext_name[80];
    // read table extension name
    if (fits_read_key(file_ptr, TSTRING, "EXTNAME", ext_name, nullptr, &status)) {
        _parse_error_message = "Table does not contain an extension name";
        ext_name[0] = '\0';
        status = 0;
    }

    // Read table dimensions
    long long rows = 0;
    int num_cols = 0;
    int total_width = 0;
    fits_get_num_rowsll(file_ptr, &rows, &status);
    fits_get_num_cols(file_ptr, &num_cols, &status);
    fits_read_key(file_ptr, TINT, "NAXIS1", &total_width, nullptr, &status);
    // Number of rows to allocate and read from the table. When reading the header, this will always be zero
    _num_rows = header_only ? 0 : rows;
    // Number of rows in the table itself
    _available_rows = rows;

    if (num_cols <= 0) {
        _parse_error_message = "Table is empty!";
        fits_close_file(file_ptr, &status);
        return false;
    }

    // Keep track of column offset when reading data
    size_t col_offset = 0;
    for (auto i = 1; i <= num_cols; i++) {
        auto& column = _columns.emplace_back(Column::FromFitsPtr(file_ptr, i, col_offset));
        // Resize column's entries vector to contain all rows
        column->Resize(_num_rows);
        // Add columns to map
        if (!column->name.empty()) {
            _column_name_map[column->name] = column.get();
        }
    }
    if (_num_rows) {
        // Read entire table into a memory buffer
        std::size_t size_bytes = total_width * _num_rows;
        auto buffer = make_unique<uint8_t[]>(size_bytes);
        fits_read_tblbytes(file_ptr, 1, 1, size_bytes, buffer.get(), &status);
        // File is no longer needed after table is read
        fits_close_file(file_ptr, &status);

        // Dynamic schedule of OpenMP division, as some columns will be easier to parse than others
        ThreadManager::ApplyThreadLimit();
#pragma omp parallel for default(none) schedule(dynamic) shared(num_cols, buffer, _num_rows, total_width)
        for (auto i = 0; i < num_cols; i++) {
            _columns[i]->FillFromBuffer(buffer.get(), _num_rows, total_width);
        }
    } else {
        fits_close_file(file_ptr, &status);
    }
    return true;
}

bool Table::IsValid() const {
    return _valid;
}

const Column* Table::GetColumnByName(const std::string& name) const {
    auto it = _column_name_map.find(name);
    if (it != _column_name_map.end()) {
        return it->second;
    }
    return nullptr;
}

const Column* Table::GetColumnById(const std::string& id) const {
    auto it = _column_id_map.find(id);
    if (it != _column_id_map.end()) {
        return it->second;
    }
    return nullptr;
}

const Column* Table::operator[](size_t i) const {
    if (i < _columns.size()) {
        return _columns[i].get();
    }
    return nullptr;
}

const Column* Table::operator[](const std::string& name_or_id) const {
    // Search first by ID and then by name
    auto id_result = GetColumnById(name_or_id);
    if (id_result) {
        return id_result;
    }
    return GetColumnByName(name_or_id);
}

size_t Table::NumColumns() const {
    return _columns.size();
}

size_t Table::NumRows() const {
    return _num_rows;
}

std::size_t Table::AvailableRows() const {
    return _available_rows;
}

const std::string& Table::Description() const {
    return _description;
}

TableView Table::View() const {
    return TableView(*this);
}

CARTA::CatalogFileType Table::Type() const {
    return _file_type;
}

std::string Table::ParseError() const {
    return _parse_error_message;
}

const CARTA::Coosys& Table::Coosys() const {
    return _coosys;
}
const std::string Table::Parameters() const {
    string parameter_string;

    for (auto& p : _params) {
        parameter_string += fmt::format("{}: {}\n", p.name, p.value);
    }
    return parameter_string;
}

} // namespace carta
