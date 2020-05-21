#include <memory>
#include "Columns.h"
#include <fitsio.h>
#include "DataColumn.tcc"

namespace carta {
using namespace std;

Column::Column(const string& name_chr) {
    name = name_chr;
    data_type = UNKNOWN_TYPE;
    data_type_size = 0;
    data_offset = 0;
}

std::unique_ptr<Column> Column::FromField(const pugi::xml_node& field) {
    auto data_type = field.attribute("datatype");
    string name = field.attribute("name").as_string();
    string array_size_string = field.attribute("arraysize").as_string();
    string type_string = data_type.as_string();

    unique_ptr<Column> column;

    if (type_string == "char") {
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
    } else {
        column = make_unique<Column>(name);
    }

    column->id = field.attribute("ID").as_string();
    column->description = field.attribute("description").as_string();
    column->unit = field.attribute("unit").as_string();
    column->ucd = field.attribute("ucd").as_string();
    return column;
}

void TrimSpaces(string& str) {
    str.erase(str.find_last_not_of(' ') + 1);
}

// Create a column based on the FITS column data type
std::unique_ptr<Column> ColumnFromFitsType(int type, const string& col_name) {
    switch (type) {
        case TBYTE: return make_unique<DataColumn<uint8_t>>(col_name);
        case TSBYTE: return make_unique<DataColumn<int8_t>>(col_name);
        case TUSHORT: return make_unique<DataColumn<uint16_t>>(col_name);
        case TSHORT: return make_unique<DataColumn<int16_t>>(col_name);
        // TODO: What are the appropriate widths for TINT and TUINT?
        case TULONG: return make_unique<DataColumn<uint32_t>>(col_name);
        case TLONG: return make_unique<DataColumn<int32_t>>(col_name);
        case TFLOAT: return make_unique<DataColumn<float>>(col_name);
        case TULONGLONG: return make_unique<DataColumn<uint64_t>>(col_name);
        case TLONGLONG: return make_unique<DataColumn<int64_t>>(col_name);
        case TDOUBLE: return make_unique<DataColumn<double>>(col_name);
        // TODO: Consider supporting complex numbers through std::complex
        case TCOMPLEX:
        case TDBLCOMPLEX:
        default: return make_unique<Column>(col_name);
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
        if (col_repeat == 1) {
            // Single-character strings are treated as byte values
            column = make_unique<DataColumn<uint8_t>>(col_name);
        } else if (col_width == col_repeat) {
            // Only support single string columns (i.e. width is same size as repeat size)
            column = make_unique<DataColumn<string>>(col_name);
            column->data_type_size = col_repeat;
        } else {
            column = ColumnFromFitsType(col_type, col_name);
            make_unique<Column>(col_name);
        }
        // Special case: for string fields, the total width is simply the repeat, and the width field indicates how many characters per sub-string
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

string Column::Info() {
    auto type_string = data_type == UNKNOWN_TYPE ? "unsupported" : data_type == STRING ? "string" : fmt::format("{} bytes per entry", data_type_size);
    auto unit_string = unit.empty() ? "" : fmt::format("Unit: {}; ", unit);
    auto description_string = description.empty() ? "" : fmt::format("Description: {}; ", description);
    return fmt::format("Name: {}; Data: {}; {}{}\n", name, type_string, unit_string, description_string);
}

// Specialisation for string type, in order to trim whitespace at the end of the entry
template<>
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

}
