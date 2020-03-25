#include "VOTableCarrier.h"

#include <cassert>
#include <set>
#include <thread>

#include "../InterfaceConstants.h"

using namespace catalog;

VOTableCarrier::VOTableCarrier() : _stream_count(0) {
    _filter_request.set_file_id(-1);
    _filter_request.set_image_file_id(-1);
    _filter_request.set_region_id(-1);
};

void VOTableCarrier::SetFileName(std::string file_path_name) {
    std::size_t found = file_path_name.find_last_of("/");
    _filename = file_path_name.substr(found + 1);
    _directory = file_path_name.substr(0, found);
}

void VOTableCarrier::FillVOTableAttributes(std::string name, std::string version) {
    if (name == "version") {
        _votable_version = version;
    }
}

void VOTableCarrier::FillFileDescription(std::string description) {
    _file_description += description;
    _file_description += ". ";
}

std::string VOTableCarrier::GetFileDescription() {
    return _file_description;
}

void VOTableCarrier::FillCoosysAttributes(int count, std::string name, std::string value) {
    if (name == "ID") {
        _coosys[count].id = value;
    } else if (name == "equinox") {
        _coosys[count].equinox = value;
    } else if (name == "epoch") {
        _coosys[count].epoch = value;
    } else if (name == "system") {
        _coosys[count].system = value;
    } else {
        std::cerr << "Can not recognize the COOSYS attribute: " << name << " : " << value << std::endl;
    }
}

void VOTableCarrier::FillFieldAttributes(int count, std::string name, std::string value) {
    if (name == "name") {
        _fields[count].name = value;
    } else if (name == "ID") {
        _fields[count].id = value;
    } else if (name == "datatype") {
        _fields[count].datatype = GetDataType(value);
    } else if (name == "arraysize") {
        _fields[count].arraysize = value;
    } else if (name == "width") {
        _fields[count].width = value;
    } else if (name == "precision") {
        _fields[count].precision = value;
    } else if (name == "xtype") {
        _fields[count].xtype = value;
    } else if (name == "unit") {
        _fields[count].unit = value;
    } else if (name == "ucd") {
        _fields[count].ucd = value;
    } else if (name == "utype") {
        _fields[count].utype = value;
    } else if (name == "ref") {
        _fields[count].ref = value;
    } else if (name == "type") {
        _fields[count].type = value;
    } else {
        std::cerr << "Can not recognize the FIELD attribute: " << name << " : " << value << std::endl;
    }
}

void VOTableCarrier::FillFieldDescriptions(int count, std::string value) {
    _fields[count].description = value;
}

void VOTableCarrier::FillTdValues(int column_index, std::string value) {
    if (_fields[column_index].datatype == CARTA::EntryType::BOOL) {
        // Convert the string to lowercase
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        _bool_vectors[column_index].push_back(value == "true");
    } else if (_fields[column_index].datatype == CARTA::EntryType::STRING) {
        _string_vectors[column_index].push_back(value);
    } else if (_fields[column_index].datatype == CARTA::EntryType::INT) {
        // PS: C++ has no function to convert the "string" to "short", so we just convert it to "int"
        try {
            _int_vectors[column_index].push_back(std::stoi(value));
        } catch (...) {
            _int_vectors[column_index].push_back(std::numeric_limits<int>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == CARTA::EntryType::LONGLONG) {
        try {
            _ll_vectors[column_index].push_back(std::stoll(value));
        } catch (...) {
            _ll_vectors[column_index].push_back(std::numeric_limits<long long>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == CARTA::EntryType::FLOAT) {
        try {
            _float_vectors[column_index].push_back(std::stof(value));
        } catch (...) {
            _float_vectors[column_index].push_back(std::numeric_limits<float>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == CARTA::EntryType::DOUBLE) {
        try {
            _double_vectors[column_index].push_back(std::stod(value));
        } catch (...) {
            _double_vectors[column_index].push_back(std::numeric_limits<double>::quiet_NaN());
        }
    } else {
        // Do not cache the table column if its data type is not in our list
    }
}

void VOTableCarrier::FillEmptyTd(int column_index) {
    if (_fields[column_index].datatype == CARTA::EntryType::STRING) {
        _string_vectors[column_index].push_back("");
    } else if (_fields[column_index].datatype == CARTA::EntryType::BOOL) {
        _bool_vectors[column_index].push_back(false);
    } else if (_fields[column_index].datatype == CARTA::EntryType::INT) {
        _int_vectors[column_index].push_back(std::numeric_limits<int>::quiet_NaN());
    } else if (_fields[column_index].datatype == CARTA::EntryType::LONGLONG) {
        _ll_vectors[column_index].push_back(std::numeric_limits<long long>::quiet_NaN());
    } else if (_fields[column_index].datatype == CARTA::EntryType::FLOAT) {
        _float_vectors[column_index].push_back(std::numeric_limits<float>::quiet_NaN());
    } else if (_fields[column_index].datatype == CARTA::EntryType::DOUBLE) {
        _double_vectors[column_index].push_back(std::numeric_limits<double>::quiet_NaN());
    }
}

void VOTableCarrier::UpdateNumOfTableRows() {
    if (_fields.empty()) {
        std::cerr << "There is no table column!" << std::endl;
        return;
    }
    for (int i = 1; i <= _fields.size(); ++i) {
        if (_bool_vectors.find(i) != _bool_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _bool_vectors[i].size();
            } else if (_num_of_rows != _bool_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        } else if (_string_vectors.find(i) != _string_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _string_vectors[i].size();
            } else if (_num_of_rows != _string_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        } else if (_int_vectors.find(i) != _int_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _int_vectors[i].size();
            } else if (_num_of_rows != _int_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        } else if (_ll_vectors.find(i) != _ll_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _ll_vectors[i].size();
            } else if (_num_of_rows != _ll_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        } else if (_float_vectors.find(i) != _float_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _float_vectors[i].size();
            } else if (_num_of_rows != _float_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        } else if (_double_vectors.find(i) != _double_vectors.end()) {
            if (i == 1) {
                _num_of_rows = _double_vectors[i].size();
            } else if (_num_of_rows != _double_vectors[i].size()) {
                std::cerr << "The columns sizes are not consistent!" << std::endl;
            }
        }
    }
}

void VOTableCarrier::GetHeaders(CARTA::CatalogFileInfoResponse& file_info_response) {
    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;
        if (tmp_field.datatype != CARTA::EntryType::UNKNOWN_TYPE) { // Only fill the header that its data type is in our list
            auto header = file_info_response.add_headers();
            header->set_name(tmp_field.name);
            header->set_data_type(tmp_field.datatype);
            header->set_column_index(field.first); // The FIELD index in the VOTable
            header->set_data_type_index(-1);       // -1 means there is no corresponding data vector in the CatalogColumnsData
            header->set_description(tmp_field.description);
            header->set_units(tmp_field.unit);
        }
    }
}

void VOTableCarrier::GetCooosys(CARTA::CatalogFileInfo* file_info) {
    if (_coosys.size() == 0) {
        std::cerr << "COOSYS does not exist!" << std::endl;
        return;
    }
    for (std::pair<int, Coosys> coosys : _coosys) {
        auto coosys_info = file_info->add_coosys();
        coosys_info->set_equinox(coosys.second.equinox);
        coosys_info->set_epoch(coosys.second.epoch);
        coosys_info->set_system(coosys.second.system);
    }
}

void VOTableCarrier::GetHeadersAndData(CARTA::OpenCatalogFileAck& open_file_response, int preview_data_size) {
    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;
        if (tmp_field.datatype != CARTA::EntryType::UNKNOWN_TYPE) { // Only fill the header that its data type is in our list
            auto header = open_file_response.add_headers();
            header->set_name(tmp_field.name);
            header->set_data_type(tmp_field.datatype);
            header->set_column_index(field.first); // The FIELD index in the VOTable
            header->set_description(tmp_field.description);
            header->set_units(tmp_field.unit);

            // Fill the column data with respect to its header
            int column_index = field.first;
            auto columns_data = open_file_response.mutable_columns_data();
            if (_bool_vectors.count(column_index)) {
                std::vector<bool>& ref_column_data = _bool_vectors[column_index];
                // Add a bool column
                auto bool_columns_data = columns_data->add_bool_column();
                // Fill bool column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    bool_columns_data->add_bool_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->bool_column_size() - 1;
                // Fill the bool column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            } else if (_string_vectors.count(column_index)) {
                std::vector<std::string>& ref_column_data = _string_vectors[column_index];
                // Add a string column
                auto string_columns_data = columns_data->add_string_column();
                // Fill string column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    string_columns_data->add_string_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->string_column_size() - 1;
                // Fill the string column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            } else if (_int_vectors.count(column_index)) {
                std::vector<int>& ref_column_data = _int_vectors[column_index];
                // Add a int column
                auto int_columns_data = columns_data->add_int_column();
                // Fill int column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    int_columns_data->add_int_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->int_column_size() - 1;
                // Fill the int column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            } else if (_ll_vectors.count(column_index)) {
                std::vector<long long>& ref_column_data = _ll_vectors[column_index];
                // Add a long long column
                auto ll_columns_data = columns_data->add_ll_column();
                // Fill long long column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    ll_columns_data->add_ll_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->ll_column_size() - 1;
                // Fill the long long column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            } else if (_float_vectors.count(column_index)) {
                std::vector<float>& ref_column_data = _float_vectors[column_index];
                // Add a float column
                auto float_columns_data = columns_data->add_float_column();
                // Fill float column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    float_columns_data->add_float_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->float_column_size() - 1;
                // Fill the float column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            } else if (_double_vectors.count(column_index)) {
                std::vector<double>& ref_column_data = _double_vectors[column_index];
                // Add a double column
                auto double_columns_data = columns_data->add_double_column();
                // Fill double column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    double_columns_data->add_double_column(ref_column_data[i]);
                }
                // Assign the mapping of column_index -> data_type_index
                _column_index_to_data_type_index[column_index] = columns_data->double_column_size() - 1;
                // Fill the double column index
                header->set_data_type_index(_column_index_to_data_type_index[column_index]);
            }
        }
    }
}

void VOTableCarrier::FilterData(const CARTA::CatalogFilterRequest& filter_request) {
    int image_file_id(filter_request.image_file_id());                              // TODO: Not implement yet
    int region_id(filter_request.region_id());                                      // TODO: Not implement yet
    CARTA::CatalogImageBounds catalog_image_bounds = filter_request.image_bounds(); // TODO: Not implement yet
    CARTA::ImageBounds image_bounds = catalog_image_bounds.image_bounds();          // TODO: Not implement yet
    std::string sort_column(filter_request.sort_column());
    CARTA::SortingType sorting_type = filter_request.sorting_type();

    // Get the total number of row
    int total_row_num = GetTableRowNumber();

    // Sort the column and set row indexes
    if (sort_column.empty()) {
        ResetRowIndexes(); // Set the default table row indexes as [0, 1, 2, ...]
    } else if (!sort_column.empty() && (sort_column != _sort_column)) {
        // Sort the column and renew the row indexes
        SortColumn(sort_column, sorting_type);
        _sort_column = sort_column;
    }

    // Initialize the "bool" vector "fill" as an int type vector
    _fill.clear();
    _fill.resize(total_row_num, 1);

    // Loop filters
    for (auto filter : filter_request.filter_configs()) {
        // Loop columns for each filter
        for (std::pair<int, Field> field : _fields) {
            if (filter.column_name() == field.second.name) {
                int column_index = field.first;
                if (_bool_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !BoolFilter(filter, _bool_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                } else if (_string_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !StringFilter(filter, _string_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                } else if (_int_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !NumericFilter<int>(filter, _int_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                } else if (_ll_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !NumericFilter<long long>(filter, _ll_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                } else if (_float_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !NumericFilter<float>(filter, _float_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                } else if (_double_vectors.count(column_index)) {
                    for (int i = 0; i < total_row_num; ++i) {
                        if (_fill[i] && !NumericFilter<double>(filter, _double_vectors[column_index][_row_indexes[i]])) {
                            _fill[i] = 0; // Do not fill this row index
                        }
                    }
                }
            }
        }
    }

    // Get the filter data size
    _filter_data_size = std::count(_fill.begin(), _fill.end(), 1);
}

void VOTableCarrier::GetFilterData2(
    CARTA::CatalogFilterRequest filter_request, std::function<void(CARTA::CatalogFilterResponse)> partial_results_callback) {
    // Renew the _fill cache if filter request changed
    if (!IsSameFilterRequest(filter_request)) {
        FilterData(filter_request);
        // Renew the _filter_request cache
        _filter_request = filter_request;
    }

    int file_id(filter_request.file_id());
    int image_file_id(filter_request.image_file_id()); // TODO: Not implement yet
    int region_id(filter_request.region_id());         // TODO: Not implement yet
    int subset_data_size(filter_request.subset_data_size());
    int subset_start_index(filter_request.subset_start_index());
    CARTA::CatalogImageBounds catalog_image_bounds = filter_request.image_bounds(); // TODO: Not implement yet
    CARTA::ImageBounds image_bounds = catalog_image_bounds.image_bounds();          // TODO: Not implement yet
    std::string sort_column(filter_request.sort_column());
    CARTA::SortingType sorting_type = filter_request.sorting_type();

    // Get column indices to hide
    std::set<int> hided_column_indices;
    for (auto hided_header : filter_request.hided_headers()) {
        for (std::pair<int, Field> field : _fields) {
            if (hided_header == field.second.name) {
                hided_column_indices.insert(field.first);
            }
        }
    }

    // Fill the filter response
    CARTA::CatalogFilterResponse filter_response;
    filter_response.set_file_id(file_id);
    filter_response.set_region_id(region_id);

    // Initialize columns data with respect to their column indices
    auto tmp_columns_data = filter_response.mutable_columns_data();

    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;

        // Only fill the columns data that its data type is in our list
        if (tmp_field.datatype != CARTA::EntryType::UNKNOWN_TYPE) {
            int column_index = field.first;
            if (_bool_vectors.count(column_index)) {
                tmp_columns_data->add_bool_column();
            } else if (_string_vectors.count(column_index)) {
                tmp_columns_data->add_string_column();
            } else if (_int_vectors.count(column_index)) {
                tmp_columns_data->add_int_column();
            } else if (_ll_vectors.count(column_index)) {
                tmp_columns_data->add_ll_column();
            } else if (_float_vectors.count(column_index)) {
                tmp_columns_data->add_float_column();
            } else if (_double_vectors.count(column_index)) {
                tmp_columns_data->add_double_column();
            }
        }
    }

    // Get the total number of row
    int total_row_num = GetTableRowNumber();
    if (subset_start_index > total_row_num - 1) {
        std::cerr << "Start row index is out of range!" << std::endl;
        return;
    }

    // Reset the subset data size with the condition
    if (subset_data_size < ALL_CATALOG_DATA) {
        std::cerr << "Subset data size is unknown!" << std::endl;
        return;
    }
    if (subset_data_size == ALL_CATALOG_DATA) {
        subset_data_size = total_row_num;
    }

    // Loop the table row data chunk by chunk
    int row_index = subset_start_index;
    int accumulated_data_size = 0;
    int sending_data_size = 0;
    int row_chunk = CATALOG_ROW_CHUNK;

    while ((accumulated_data_size < subset_data_size) && (row_index < total_row_num)) {
        // Break the loop while closing the file
        if (!_connected) {
            break;
        }

        // Reset the row chunk if it reaches the end of data
        if ((row_index + row_chunk) > total_row_num) {
            row_chunk = total_row_num - row_index;
        }

        // Get the sending data size
        sending_data_size = std::count(_fill.begin() + row_index, _fill.begin() + row_index + row_chunk, 1);

        // Reset the sending data size if it reaches the end of required subset data
        if ((accumulated_data_size + sending_data_size) > subset_data_size) {
            sending_data_size = subset_data_size - accumulated_data_size;
            accumulated_data_size = subset_data_size;
            // Reset the row chunk to fit the sending data size
            row_chunk = 0;
            int row_count = 0;
            while (row_count < sending_data_size) {
                if (_fill[row_index + row_chunk]) {
                    ++row_count;
                }
                ++row_chunk;
            }
        } else {
            accumulated_data_size += sending_data_size;
        }

        // Set the progress
        float progress;
        if ((row_index + row_chunk) >= total_row_num) {
            progress = 1.0;
        } else {
            progress = (float)(row_index + row_chunk - subset_start_index) / (float)subset_data_size;
        }

        // Fill the row data chunk by chunk
        for (std::pair<int, std::vector<bool>> bool_vector : _bool_vectors) {
            int column_index = bool_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->bool_column_size());
                auto bool_column = tmp_columns_data->mutable_bool_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        bool_column->add_bool_column(bool_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<std::string>> string_vector : _string_vectors) {
            int column_index = string_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->string_column_size());
                auto string_column = tmp_columns_data->mutable_string_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        string_column->add_string_column(string_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<int>> int_vector : _int_vectors) {
            int column_index = int_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->int_column_size());
                auto int_column = tmp_columns_data->mutable_int_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        int_column->add_int_column(int_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<long long>> ll_vector : _ll_vectors) {
            int column_index = ll_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->ll_column_size());
                auto ll_column = tmp_columns_data->mutable_ll_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        ll_column->add_ll_column(ll_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<float>> float_vector : _float_vectors) {
            int column_index = float_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->float_column_size());
                auto float_column = tmp_columns_data->mutable_float_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        float_column->add_float_column(float_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<double>> double_vector : _double_vectors) {
            int column_index = double_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->double_column_size());
                auto double_column = tmp_columns_data->mutable_double_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (_fill[i]) {
                        double_column->add_double_column(double_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }

        // Proceed to the next chunk
        row_index += row_chunk;

        // Fill the progress message
        filter_response.set_subset_data_size(sending_data_size);
        filter_response.set_subset_end_index(row_index);
        filter_response.set_progress(progress);
        filter_response.set_filter_data_size(_filter_data_size);

        // Send partial results by the callback function
        partial_results_callback(filter_response);

        // Clear the columns_data message
        for (int i = 0; i < tmp_columns_data->bool_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_bool_column(i);
            tmp_column->clear_bool_column();
        }
        for (int i = 0; i < tmp_columns_data->string_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_string_column(i);
            tmp_column->clear_string_column();
        }
        for (int i = 0; i < tmp_columns_data->int_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_int_column(i);
            tmp_column->clear_int_column();
        }
        for (int i = 0; i < tmp_columns_data->ll_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_ll_column(i);
            tmp_column->clear_ll_column();
        }
        for (int i = 0; i < tmp_columns_data->float_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_float_column(i);
            tmp_column->clear_float_column();
        }
        for (int i = 0; i < tmp_columns_data->double_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_double_column(i);
            tmp_column->clear_double_column();
        }
    }
}

void VOTableCarrier::GetFilterData(
    CARTA::CatalogFilterRequest filter_request, std::function<void(CARTA::CatalogFilterResponse)> partial_results_callback) {
    int file_id(filter_request.file_id());
    int region_id(filter_request.region_id()); // TODO: Not implement yet
    int subset_data_size(filter_request.subset_data_size());
    int subset_start_index(filter_request.subset_start_index());
    CARTA::CatalogImageBounds catalog_image_bounds = filter_request.image_bounds(); // TODO: Not implement yet
    CARTA::ImageBounds image_bounds = catalog_image_bounds.image_bounds();
    std::string sort_column(filter_request.sort_column());
    CARTA::SortingType sorting_type = filter_request.sorting_type();

    // Get column indices to hide
    std::set<int> hided_column_indices;
    for (auto hided_header : filter_request.hided_headers()) {
        for (std::pair<int, Field> field : _fields) {
            if (hided_header == field.second.name) {
                hided_column_indices.insert(field.first);
            }
        }
    }

    // Fill the filter response
    CARTA::CatalogFilterResponse filter_response;
    filter_response.set_file_id(file_id);
    filter_response.set_region_id(region_id);

    // Initialize columns data with respect to their column indices
    auto tmp_columns_data = filter_response.mutable_columns_data();

    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;

        // Only fill the columns data that its data type is in our list
        if (tmp_field.datatype != CARTA::EntryType::UNKNOWN_TYPE) {
            int column_index = field.first;
            if (_bool_vectors.count(column_index)) {
                tmp_columns_data->add_bool_column();
            } else if (_string_vectors.count(column_index)) {
                tmp_columns_data->add_string_column();
            } else if (_int_vectors.count(column_index)) {
                tmp_columns_data->add_int_column();
            } else if (_ll_vectors.count(column_index)) {
                tmp_columns_data->add_ll_column();
            } else if (_float_vectors.count(column_index)) {
                tmp_columns_data->add_float_column();
            } else if (_double_vectors.count(column_index)) {
                tmp_columns_data->add_double_column();
            }
        }
    }

    // Get the total number of row
    int total_row_num = GetTableRowNumber();
    if (subset_start_index > total_row_num - 1) {
        std::cerr << "Start row index is out of range!" << std::endl;
        return;
    }

    // Reset the subset data size with the condition
    if (subset_data_size < ALL_CATALOG_DATA) {
        std::cerr << "Subset data size is unknown!" << std::endl;
        return;
    }
    if (subset_data_size == ALL_CATALOG_DATA) {
        subset_data_size = total_row_num;
    }

    // Sort the column and set row indexes
    if (sort_column.empty()) {
        ResetRowIndexes(); // Set the default table row indexes as [0, 1, 2, ...]
    } else if (!sort_column.empty() && (sort_column != _sort_column)) {
        // Sort the column and renew the row indexes
        SortColumn(sort_column, sorting_type);
        _sort_column = sort_column;
    }

    // Loop the table row data chunk by chunk
    int row_index = subset_start_index;
    int accumulated_data_size = 0;
    int sending_data_size = 0;
    int row_chunk = CATALOG_ROW_CHUNK;

    while ((accumulated_data_size < subset_data_size) && (row_index < total_row_num)) {
        // Break the loop while closing the file
        if (!_connected) {
            break;
        }

        // Reset the row chunk if it reaches the end of data
        if ((row_index + row_chunk) > total_row_num) {
            row_chunk = total_row_num - row_index;
        }

        // Initialize the "bool" vector "fill" as an int type vector
        std::vector<int> fill(row_chunk, 1);

        // Loop filters
        for (auto filter : filter_request.filter_configs()) {
            // Loop columns for each filter
            for (std::pair<int, Field> field : _fields) {
                if (filter.column_name() == field.second.name) {
                    int column_index = field.first;
                    if (_bool_vectors.count(column_index)) {
                        // Loop a row chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !BoolFilter(filter, _bool_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    } else if (_string_vectors.count(column_index)) {
                        // Loop a row chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !StringFilter(filter, _string_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    } else if (_int_vectors.count(column_index)) {
                        // Loop a row chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !NumericFilter<int>(filter, _int_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    } else if (_ll_vectors.count(column_index)) {
                        // Loop a row chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !NumericFilter<long long>(filter, _ll_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    } else if (_float_vectors.count(column_index)) {
                        // Loop a row chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !NumericFilter<float>(filter, _float_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    } else if (_double_vectors.count(column_index)) {
                        // Loop a row data chunk with the filter
                        for (int i = row_index; i < (row_index + row_chunk); ++i) {
                            int j = i - row_index;
                            if (fill[j] && !NumericFilter<double>(filter, _double_vectors[column_index][_row_indexes[i]])) {
                                fill[j] = 0; // Do not fill this row index
                            }
                        }
                    }
                }
            }
        }

        // Get the sending data size
        sending_data_size = std::count(fill.begin(), fill.end(), 1);

        // Reset the sending data size if it reaches the end of required subset data
        if ((accumulated_data_size + sending_data_size) > subset_data_size) {
            sending_data_size = subset_data_size - accumulated_data_size;
            accumulated_data_size = subset_data_size;
            // Reset the row chunk to fit the sending data size
            row_chunk = 0;
            int row_count = 0;
            while (row_count < sending_data_size) {
                if (fill[row_chunk]) {
                    ++row_count;
                }
                ++row_chunk;
            }
        } else {
            accumulated_data_size += sending_data_size;
        }

        // Set the progress
        float progress;
        if ((row_index + row_chunk) >= total_row_num) {
            progress = 1.0;
        } else {
            progress = (float)(row_index + row_chunk - subset_start_index) / (float)subset_data_size;
        }

        // Fill the row data chunk by chunk
        for (std::pair<int, std::vector<bool>> bool_vector : _bool_vectors) {
            int column_index = bool_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->bool_column_size());
                auto bool_column = tmp_columns_data->mutable_bool_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        bool_column->add_bool_column(bool_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<std::string>> string_vector : _string_vectors) {
            int column_index = string_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->string_column_size());
                auto string_column = tmp_columns_data->mutable_string_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        string_column->add_string_column(string_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<int>> int_vector : _int_vectors) {
            int column_index = int_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->int_column_size());
                auto int_column = tmp_columns_data->mutable_int_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        int_column->add_int_column(int_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<long long>> ll_vector : _ll_vectors) {
            int column_index = ll_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->ll_column_size());
                auto ll_column = tmp_columns_data->mutable_ll_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        ll_column->add_ll_column(ll_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<float>> float_vector : _float_vectors) {
            int column_index = float_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->float_column_size());
                auto float_column = tmp_columns_data->mutable_float_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        float_column->add_float_column(float_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }
        for (std::pair<int, std::vector<double>> double_vector : _double_vectors) {
            int column_index = double_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = _column_index_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->double_column_size());
                auto double_column = tmp_columns_data->mutable_double_column(data_type_index);
                for (int i = row_index; i < (row_index + row_chunk); ++i) {
                    if (fill[i - row_index]) {
                        double_column->add_double_column(double_vector.second[_row_indexes[i]]);
                    }
                }
            }
        }

        // Proceed to the next chunk
        row_index += row_chunk;

        // Fill the progress message
        filter_response.set_subset_data_size(sending_data_size);
        filter_response.set_subset_end_index(row_index);
        filter_response.set_progress(progress);
        filter_response.set_filter_data_size(accumulated_data_size);

        // Send partial results by the callback function
        partial_results_callback(filter_response);

        // Clear the columns_data message
        for (int i = 0; i < tmp_columns_data->bool_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_bool_column(i);
            tmp_column->clear_bool_column();
        }
        for (int i = 0; i < tmp_columns_data->string_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_string_column(i);
            tmp_column->clear_string_column();
        }
        for (int i = 0; i < tmp_columns_data->int_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_int_column(i);
            tmp_column->clear_int_column();
        }
        for (int i = 0; i < tmp_columns_data->ll_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_ll_column(i);
            tmp_column->clear_ll_column();
        }
        for (int i = 0; i < tmp_columns_data->float_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_float_column(i);
            tmp_column->clear_float_column();
        }
        for (int i = 0; i < tmp_columns_data->double_column_size(); ++i) {
            auto tmp_column = tmp_columns_data->mutable_double_column(i);
            tmp_column->clear_double_column();
        }
    }
}

size_t VOTableCarrier::GetTableRowNumber() {
    UpdateNumOfTableRows();
    return _num_of_rows;
}

CARTA::EntryType VOTableCarrier::GetDataType(const std::string& data_type) {
    if (data_type == "boolean") {
        return CARTA::EntryType::BOOL;
    } else if (data_type == "char") {
        return CARTA::EntryType::STRING;
    } else if (data_type == "short" || data_type == "int") {
        return CARTA::EntryType::INT;
    } else if (data_type == "long") {
        return CARTA::EntryType::LONGLONG;
    } else if (data_type == "float") {
        return CARTA::EntryType::FLOAT;
    } else if (data_type == "double") {
        return CARTA::EntryType::DOUBLE;
    } else {
        return CARTA::EntryType::UNKNOWN_TYPE;
    }
}

bool VOTableCarrier::IsValid() {
    // Empty column header is identified as a NOT valid VOTable file
    return (!_fields.empty());
}

void VOTableCarrier::PrintTableElement(int row, int column) {
    if (_bool_vectors.find(column) != _bool_vectors.end()) {
        std::cout << _bool_vectors[column][row] << " | ";
    } else if (_string_vectors.find(column) != _string_vectors.end()) {
        std::cout << _string_vectors[column][row] << " | ";
    } else if (_int_vectors.find(column) != _int_vectors.end()) {
        std::cout << _int_vectors[column][row] << " | ";
    } else if (_ll_vectors.find(column) != _ll_vectors.end()) {
        std::cout << _ll_vectors[column][row] << " | ";
    } else if (_float_vectors.find(column) != _float_vectors.end()) {
        std::cout << _float_vectors[column][row] << " | ";
    } else if (_double_vectors.find(column) != _double_vectors.end()) {
        std::cout << _double_vectors[column][row] << " | ";
    } else {
        std::cout << " | ";
    }
}

void VOTableCarrier::PrintData() {
    UpdateNumOfTableRows();
    std::cout << "------------------------------------------------------------------\n";
    std::cout << "File Name              : " << _filename << std::endl;
    std::cout << "File Directory         : " << _directory << std::endl;
    std::cout << "VOTable Version        : " << _votable_version << std::endl;
    std::cout << "Table column size      : " << _fields.size() << std::endl;
    std::cout << "Table row size         : " << _num_of_rows << std::endl;
    std::cout << "------------------------------------------------------------------\n";
    std::cout << "# of bool columns      : " << _bool_vectors.size() << std::endl;
    std::cout << "# of string columns    : " << _string_vectors.size() << std::endl;
    std::cout << "# of int columns       : " << _int_vectors.size() << std::endl;
    std::cout << "# of long long columns : " << _ll_vectors.size() << std::endl;
    std::cout << "# of float columns     : " << _float_vectors.size() << std::endl;
    std::cout << "# of double columns    : " << _double_vectors.size() << std::endl;
    std::cout << "------------------------------------------------------------------\n";
    // Print coordinate systems
    for (std::pair<int, Coosys> coosys : _coosys) {
        std::cout << "Coosys(" << coosys.first << "): " << std::endl;
        coosys.second.Print();
        std::cout << "------------------------------------------------------------------\n";
    }
    // Print table fields (column definitions)
    for (std::pair<int, Field> field : _fields) {
        std::cout << "Field(" << field.first << "): " << std::endl;
        field.second.Print();
        std::cout << "------------------------------------------------------------------\n";
    }
    // Print table rows
    for (int i = 0; i < _num_of_rows; ++i) {
        std::cout << "row " << i << ": | ";
        for (int j = 1; j <= _fields.size(); ++j) {
            PrintTableElement(i, j);
        }
        std::cout << "\n------------------------------------------------------------------\n";
    }
}

bool VOTableCarrier::BoolFilter(CARTA::FilterConfig filter, bool value) {
    // TODO: To be defined
    return true;
}

bool VOTableCarrier::StringFilter(const CARTA::FilterConfig& filter, const std::string& value) {
    std::string sub_string = filter.sub_string();
    return (value.find(sub_string) != std::string::npos);
}

template <typename T>
bool VOTableCarrier::NumericFilter(const CARTA::FilterConfig& filter, const T& value) {
    bool result(true);
    CARTA::ComparisonOperator cmp_op = filter.comparison_operator();
    switch (cmp_op) {
        case CARTA::ComparisonOperator::EqualTo:
            result = (value == filter.min());
            break;
        case CARTA::ComparisonOperator::NotEqualTo:
            result = (value != filter.min());
            break;
        case CARTA::ComparisonOperator::LessThan:
            result = (value < filter.min());
            break;
        case CARTA::ComparisonOperator::GreaterThan:
            result = (value > filter.min());
            break;
        case CARTA::ComparisonOperator::LessThanOrEqualTo:
            result = (value <= filter.min());
            break;
        case CARTA::ComparisonOperator::GreaterThanOrEqualTo:
            result = (value >= filter.min());
            break;
        case CARTA::ComparisonOperator::BetweenAnd:
            result = (filter.min() <= value && value <= filter.max());
            break;
        case CARTA::ComparisonOperator::FromTo:
            result = (filter.min() < value && value < filter.max());
            break;
        default:
            std::cerr << "Unknown comparison operator!" << std::endl;
            break;
    }
    return result;
}

void VOTableCarrier::SortColumn(std::string column_name, CARTA::SortingType sorting_type) {
    int column_index;
    bool found(false);
    for (std::pair<int, Field> field : _fields) {
        if (column_name == field.second.name) {
            found = true;
            column_index = field.first;
            if (_int_vectors.count(column_index)) {
                SortRowIndexes(_int_vectors[column_index], sorting_type);
            } else if (_ll_vectors.count(column_index)) {
                SortRowIndexes(_ll_vectors[column_index], sorting_type);
            } else if (_float_vectors.count(column_index)) {
                SortRowIndexes(_float_vectors[column_index], sorting_type);
            } else if (_double_vectors.count(column_index)) {
                SortRowIndexes(_double_vectors[column_index], sorting_type);
            } else {
                std::cout << "Column data are NOT NUMERICAL!" << std::endl;
                ResetRowIndexes(); // Set the default row indexes as [0, 1, 2, ...]
            }
        }
    }
    if (!found) {
        std::cout << "Column data are NOT FOUND!" << std::endl;
        ResetRowIndexes(); // Set the default row indexes as [0, 1, 2, ...]
    }
}

template <typename T>
void VOTableCarrier::SortRowIndexes(const std::vector<T>& v, CARTA::SortingType sorting_type) {
    ResetRowIndexes();
    // Sort the column data and get the result as an index array
    if (sorting_type == CARTA::SortingType::Ascend) {
        std::stable_sort(_row_indexes.begin(), _row_indexes.end(), [&v](size_t i1, size_t i2) { return v[i1] < v[i2]; });
    } else {
        std::stable_sort(_row_indexes.begin(), _row_indexes.end(), [&v](size_t i1, size_t i2) { return v[i1] > v[i2]; });
    }
}

void VOTableCarrier::ResetRowIndexes() {
    // Initialize original index locations
    _row_indexes.resize(GetTableRowNumber());
    std::iota(_row_indexes.begin(), _row_indexes.end(), 0);
}

void VOTableCarrier::SetConnectionFlag(bool connected) {
    _connected = connected;
}

void VOTableCarrier::DisconnectCalled() {
    SetConnectionFlag(false); // set a false flag to interrupt the running jobs
    while (_stream_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // wait for the jobs finished
}

bool VOTableCarrier::IsSameFilterRequest(const CARTA::CatalogFilterRequest& filter_request) {
    // Check file_id
    if (_filter_request.file_id() != filter_request.file_id()) {
        return false;
    }
    // Check hided_headers
    if (_filter_request.hided_headers_size() != filter_request.hided_headers_size()) {
        return false;
    } else {
        for (int i = 0; i < filter_request.hided_headers_size(); ++i) {
            if (_filter_request.hided_headers(i) != filter_request.hided_headers(i)) {
                return false;
            }
        }
    }
    // Check filter_configs
    if (_filter_request.filter_configs_size() > 0 || filter_request.filter_configs_size() > 0) {
        if (_filter_request.filter_configs_size() != filter_request.filter_configs_size()) {
            return false;
        } else {
            for (int i = 0; i < filter_request.filter_configs_size(); ++i) {
                auto m_filter_configs = _filter_request.filter_configs(i);
                auto filter_configs = filter_request.filter_configs(i);
                if ((!m_filter_configs.column_name().empty() && !filter_configs.column_name().empty()) &&
                    (m_filter_configs.column_name() != filter_configs.column_name())) {
                    return false;
                }
                if (m_filter_configs.comparison_operator() != filter_configs.comparison_operator()) {
                    return false;
                }
                if (m_filter_configs.min() != filter_configs.min()) {
                    return false;
                }
                if (m_filter_configs.max() != filter_configs.max()) {
                    return false;
                }
                if ((!m_filter_configs.sub_string().empty() && !filter_configs.sub_string().empty()) &&
                    (m_filter_configs.sub_string() != filter_configs.sub_string())) {
                    return false;
                }
            }
        }
    }
    // Check image_bounds
    auto _image_bounds = _filter_request.image_bounds();
    auto image_bounds = filter_request.image_bounds();
    if ((!_image_bounds.x_column_name().empty() && !image_bounds.x_column_name().empty()) &&
        (_image_bounds.x_column_name() != image_bounds.x_column_name())) {
        return false;
    }
    if ((!_image_bounds.y_column_name().empty() && !image_bounds.y_column_name().empty()) &&
        (_image_bounds.y_column_name() != image_bounds.y_column_name())) {
        return false;
    }
    if (_image_bounds.image_bounds().x_min() != image_bounds.image_bounds().x_min()) {
        return false;
    }
    if (_image_bounds.image_bounds().x_max() != image_bounds.image_bounds().x_max()) {
        return false;
    }
    if (_image_bounds.image_bounds().y_min() != image_bounds.image_bounds().y_min()) {
        return false;
    }
    if (_image_bounds.image_bounds().y_max() != image_bounds.image_bounds().y_max()) {
        return false;
    }
    // Check image_file_id
    if (_filter_request.image_file_id() != filter_request.image_file_id()) {
        return false;
    }
    // Check region_id
    if (_filter_request.region_id() != filter_request.region_id()) {
        return false;
    }
    // Check sort_column and sorting_type
    if (!_filter_request.sort_column().empty() && !filter_request.sort_column().empty()) {
        if (_filter_request.sort_column() != filter_request.sort_column()) {
            return false;
        } else {
            if (_filter_request.sorting_type() != filter_request.sorting_type()) {
                return false;
            }
        }
    }

    return true;
}