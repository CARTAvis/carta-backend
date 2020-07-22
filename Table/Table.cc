#include "Table.h"

#include <fitsio.h>
#include <fmt/format.h>

#include <iostream>

#include "../Util.h"
#include "DataColumn.tcc"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace carta {
using namespace std;

Table::Table(const string& filename, bool header_only) : _valid(false), _filename(filename), _num_rows(0) {
    fs::path file_path(filename);

    if (!fs::exists(file_path)) {
        _parse_error_message = "File does not exist!";
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
            fmt::print("{}\n", result.description());
            return false;
        }
    } else {
        auto result = doc.load_file(_filename.c_str(), pugi::parse_default | pugi::parse_embed_pcdata);
        if (!result) {
            fmt::print("{}\n", result.description());
            return false;
        }
    }

    auto votable = doc.child("VOTABLE");

    if (!votable) {
        _parse_error_message = "Missing XML element VOTABLE!";
        return false;
    }

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

    if (!PopulateFields(table_node)) {
        _parse_error_message = "Cannot parse table headers!";
        return false;
    }

    // Once fields are populated, stop parsing
    if (header_only) {
        return true;
    }

    if (!PopulateRows(table_node)) {
        _parse_error_message = "Cannot parse table data!";
        return false;
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
    _num_rows = header_only ? 0 : rows;

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

} // namespace carta
