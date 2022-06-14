/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Columns.h"

#include <memory>

#include <fitsio.h>
#include <spdlog/fmt/fmt.h>

#include "DataColumn.tcc"
#include "ThreadingManager/ThreadingManager.h"

namespace carta {

Column::Column(const string& name_chr) {
    name = name_chr;
    data_type = CARTA::UnsupportedType;
    data_type_size = 0;
    data_offset = 0;
}

std::unique_ptr<Column> Column::FromField(const pugi::xml_node& field) {
    auto data_type = field.attribute("datatype");
    string name = field.attribute("name").as_string();
    string array_size_string = field.attribute("arraysize").as_string();
    string type_string = data_type.as_string();

    unique_ptr<Column> column;

    if (type_string == "char" || type_string == "unicodeChar") {
        column = make_unique<DataColumn<string>>(name);
    } else if (!array_size_string.empty()) {
        // Can't support array-based column types other than char
        column = make_unique<Column>(name);
    } else if (type_string == "int") {
        column = make_unique<DataColumn<int32_t>>(name);
    } else if (type_string == "short") {
        column = make_unique<DataColumn<int16_t>>(name);
    } else if (type_string == "unsignedByte") {
        column = make_unique<DataColumn<uint8_t>>(name);
    } else if (type_string == "long") {
        column = make_unique<DataColumn<int64_t>>(name);
    } else if (type_string == "float") {
        column = make_unique<DataColumn<float>>(name);
    } else if (type_string == "double") {
        column = make_unique<DataColumn<double>>(name);
    } else if (type_string == "boolean") {
        column = make_unique<DataColumn<uint8_t>>(name, true);
    } else {
        column = make_unique<Column>(name);
    }

    column->id = field.attribute("ID").as_string();
    auto description_node = field.child("DESCRIPTION");
    if (description_node) {
        column->description = description_node.child_value();
    } else {
        column->description = field.attribute("description").as_string();
    }
    column->unit = field.attribute("unit").as_string();
    column->ucd = field.attribute("ucd").as_string();
    return column;
}

std::unique_ptr<Column> Column::FromValues(const std::vector<string>& values, string name) {
    auto data_column_ptr = make_unique<DataColumn<string>>(name);
    data_column_ptr->Resize(values.size());
    if (data_column_ptr != nullptr) {
        for (auto row_index = 0; row_index < values.size(); row_index++) {
            data_column_ptr->SetFromValue(values[row_index], row_index);
        }
    }

    data_column_ptr->id = "";
    data_column_ptr->description = "";
    data_column_ptr->unit = "";
    data_column_ptr->ucd = "";
    return data_column_ptr;
}

void TrimSpaces(string& str) {
    str.erase(str.find_last_not_of(' ') + 1);
}

// Create a column based on the FITS column data type
std::unique_ptr<Column> ColumnFromFitsType(int type, const string& col_name) {
    switch (type) {
        case TBYTE:
            return make_unique<DataColumn<uint8_t>>(col_name);
        case TSBYTE:
            return make_unique<DataColumn<int8_t>>(col_name);
        case TUSHORT:
            return make_unique<DataColumn<uint16_t>>(col_name);
        case TSHORT:
            return make_unique<DataColumn<int16_t>>(col_name);
            // TODO: What are the appropriate widths for TINT and TUINT?
        case TULONG:
            return make_unique<DataColumn<uint32_t>>(col_name);
        case TLONG:
            return make_unique<DataColumn<int32_t>>(col_name);
        case TFLOAT:
            return make_unique<DataColumn<float>>(col_name);
#ifdef TULONGLONG
        case TULONGLONG:
            return make_unique<DataColumn<uint64_t>>(col_name);
#endif
        case TLONGLONG:
            return make_unique<DataColumn<int64_t>>(col_name);
        case TDOUBLE:
            return make_unique<DataColumn<double>>(col_name);
        case TLOGICAL:
            return make_unique<DataColumn<u_int8_t>>(col_name, true);
            // TODO: Consider supporting complex numbers through std::complex
        case TCOMPLEX:
        case TDBLCOMPLEX:
        default:
            return make_unique<Column>(col_name);
    }
}

std::unique_ptr<Column> Column::FromFitsPtr(fitsfile* fits_ptr, int column_index, size_t& data_offset) {
    int status = 0;
    char col_name[80];
    char unit[80];
    int col_type;
    long col_repeat;
    long col_width;
    // get column name, unit, type and sizes
    fits_get_bcolparms(fits_ptr, column_index, col_name, unit, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &status);
    fits_get_coltype(fits_ptr, column_index, &col_type, &col_repeat, &col_width, &status);

    // For non-string fields, the total width of the column is simply the size of one element (width) multiplied by the repeat count
    auto total_column_width = col_repeat * col_width;

    unique_ptr<Column> column;

    if (col_type == TSTRING) {
        if (col_width == col_repeat) {
            // Only support single string columns (i.e. width is same size as repeat size)
            column = make_unique<DataColumn<string>>(col_name);
            column->data_type_size = col_repeat;
        } else {
            column = ColumnFromFitsType(col_type, col_name);
            make_unique<Column>(col_name);
        }
        // Special case: for string fields, the total width is simply the repeat, and the width field indicates how many characters per
        // sub-string
        total_column_width = col_repeat;
    } else if (col_repeat > 1) {
        // Can't support array-based column types
        column = make_unique<Column>(col_name);
    } else {
        column = ColumnFromFitsType(col_type, col_name);
    }

    column->data_offset = data_offset;
    column->unit = unit;
    TrimSpaces(column->unit);

    // Optional keywords for VOTable compatibility: description and UCD
    char keyword[80];
    fits_read_key(fits_ptr, TSTRING, fmt::format("TCOMM{}", column_index).c_str(), keyword, nullptr, &status);
    column->description = keyword;
    fits_read_key(fits_ptr, TSTRING, fmt::format("TUCD{}", column_index).c_str(), keyword, nullptr, &status);
    column->ucd = keyword;

    TrimSpaces(column->description);
    TrimSpaces(column->ucd);

    // increment data offset for the next column
    data_offset += total_column_width;
    return column;
}

// Specialisation for string type, in order to trim whitespace at the end of the entry
template <>
void DataColumn<string>::FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride) {
    // Shifts by the column's offset
    ptr += data_offset;

    if (!stride || !data_type_size || num_rows > entries.size()) {
        return;
    }

    for (auto i = 0; i < num_rows; i++) {
        auto& s = entries[i];

        int string_size = 0;
        // Find required string size by trimming whitespace
        for (auto j = data_type_size - 1; j >= 0; j--) {
            if (ptr[j] != ' ') {
                string_size = j + 1;
                break;
            }
        }
        // Fill string with the trimmed substring
        if (string_size > 0) {
            s.resize(string_size);
            memcpy(s.data(), ptr, string_size);
        }
        ptr += stride;
    }
}

// Specialisation for strings because they don't support std::isnan
template <>
void DataColumn<string>::SortIndices(IndexList& indices, bool ascending) const {
    if (indices.empty() || entries.empty()) {
        return;
    }

    // Perform ascending or descending sort
    if (ascending) {
        parallel_sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) { return entries[a] < entries[b]; });
    } else {
        parallel_sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) { return entries[a] > entries[b]; });
    }
}

// Specialisation for logical type, because we need to convert from T/F characters to bool.
// Logical type stored in uint8_t, to avoid std::vector<bool> complexities.
template <>
void DataColumn<uint8_t>::FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride) {
    // Shifts by the column's offset
    ptr += data_offset;

    if (!stride || !data_type_size || num_rows > entries.size()) {
        return;
    }

    if (_is_logical_field) {
        for (auto i = 0; i < num_rows; i++) {
            char val = *ptr;
            entries[i] = (val == 'T');
            ptr += stride;
        }
    } else {
        for (auto i = 0; i < num_rows; i++) {
            entries[i] = *ptr;
            ptr += stride;
        }
    }
}

// String is a special case, because we store the data as a repeated string field instead of binary data
template <>
void DataColumn<std::string>::FillColumnData(
    CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const {
    column_data.set_data_type(CARTA::String);
    auto values = GetColumnData(fill_subset, indices, start, end);
    *column_data.mutable_string_data() = {values.begin(), values.end()};
}

} // namespace carta
