#include "VOTableCarrier.h"

#include <cassert>
#include <set>

using namespace catalog;

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
        _fields[count].datatype = value;
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
    if (_fields[column_index].datatype == "boolean") {
        // Convert the string to lowercase
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        _bool_vectors[column_index].push_back(value == "true");
    } else if (_fields[column_index].datatype == "char") {
        _string_vectors[column_index].push_back(value);
    } else if ((_fields[column_index].datatype == "short") || (_fields[column_index].datatype == "int")) {
        // PS: C++ has no function to convert the "string" to "short", so we just convert it to "int"
        try {
            _int_vectors[column_index].push_back(std::stoi(value));
        } catch (...) {
            _int_vectors[column_index].push_back(std::numeric_limits<int>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == "long") {
        try {
            _ll_vectors[column_index].push_back(std::stoll(value));
        } catch (...) {
            _ll_vectors[column_index].push_back(std::numeric_limits<long long>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == "float") {
        try {
            _float_vectors[column_index].push_back(std::stof(value));
        } catch (...) {
            _float_vectors[column_index].push_back(std::numeric_limits<float>::quiet_NaN());
        }
    } else if (_fields[column_index].datatype == "double") {
        try {
            _double_vectors[column_index].push_back(std::stod(value));
        } catch (...) {
            _double_vectors[column_index].push_back(std::numeric_limits<double>::quiet_NaN());
        }
    } else {
        // Do not cache the table column if its data type is not in our list
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
        CARTA::EntryType catalog_data_type;
        GetDataType(tmp_field.datatype, catalog_data_type);
        if (catalog_data_type != CARTA::EntryType::UNKNOWN_TYPE) { // Only fill the header that its data type is in our list
            auto header = file_info_response.add_headers();
            header->set_name(tmp_field.name);
            header->set_data_type(catalog_data_type);
            header->set_column_index(field.first); // The FIELD index in the VOTable
            header->set_data_type_index(-1);       // -1 means there is no corresponding data vector in the CatalogColumnsData
            header->set_description(tmp_field.description);
            header->set_units(tmp_field.unit);
        }
    }
}

void VOTableCarrier::GetHeadersAndData(CARTA::OpenCatalogFileAck& open_file_response, int preview_data_size) {
    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;
        CARTA::EntryType data_type;
        GetDataType(tmp_field.datatype, data_type);
        if (data_type != CARTA::EntryType::UNKNOWN_TYPE) { // Only fill the header that its data type is in our list
            auto header = open_file_response.add_headers();
            header->set_name(tmp_field.name);
            header->set_data_type(data_type);
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
                // Fill the bool column index
                header->set_data_type_index(bool_columns_data->bool_column_size() - 1);
            } else if (_string_vectors.count(column_index)) {
                std::vector<std::string>& ref_column_data = _string_vectors[column_index];
                // Add a string column
                auto string_columns_data = columns_data->add_string_column();
                // Fill string column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    string_columns_data->add_string_column(ref_column_data[i]);
                }
                // Fill the string column index
                header->set_data_type_index(string_columns_data->string_column_size() - 1);
            } else if (_int_vectors.count(column_index)) {
                std::vector<int>& ref_column_data = _int_vectors[column_index];
                // Add a int column
                auto int_columns_data = columns_data->add_int_column();
                // Fill int column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    int_columns_data->add_int_column(ref_column_data[i]);
                }
                // Fill the int column index
                header->set_data_type_index(int_columns_data->int_column_size() - 1);
            } else if (_ll_vectors.count(column_index)) {
                std::vector<long long>& ref_column_data = _ll_vectors[column_index];
                // Add a long long column
                auto ll_columns_data = columns_data->add_ll_column();
                // Fill long long column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    ll_columns_data->add_ll_column(ref_column_data[i]);
                }
                // Fill the long long column index
                header->set_data_type_index(ll_columns_data->ll_column_size() - 1);
            } else if (_float_vectors.count(column_index)) {
                std::vector<float>& ref_column_data = _float_vectors[column_index];
                // Add a float column
                auto float_columns_data = columns_data->add_float_column();
                // Fill float column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    float_columns_data->add_float_column(ref_column_data[i]);
                }
                // Fill the float column index
                header->set_data_type_index(float_columns_data->float_column_size() - 1);
            } else if (_double_vectors.count(column_index)) {
                std::vector<double>& ref_column_data = _double_vectors[column_index];
                // Add a double column
                auto double_columns_data = columns_data->add_double_column();
                // Fill double column elements
                for (int i = 0; i < preview_data_size; ++i) {
                    double_columns_data->add_double_column(ref_column_data[i]);
                }
                // Fill the double column index
                header->set_data_type_index(double_columns_data->double_column_size() - 1);
            }
        }
    }
}

void VOTableCarrier::GetFilteredData(
    CARTA::CatalogFilterRequest filter_request, std::function<void(CARTA::CatalogFilterResponse)> partial_results_callback) {
    int file_id(filter_request.file_id());
    int region_id(filter_request.region_id()); // TODO: Not implement yet
    int subset_data_size(filter_request.subset_data_size());
    int subset_start_index(filter_request.subset_start_index());
    CARTA::CatalogImageBounds catalog_image_bounds = filter_request.image_bounds();
    CARTA::ImageBounds image_bounds = catalog_image_bounds.image_bounds();

    // Get column indices to hide
    std::set<int> hided_column_indices;
    for (int i = 0; i < filter_request.hided_headers_size(); ++i) {
        std::string hided_header = filter_request.hided_headers(i);
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
    int bool_vector_index = 0;
    int string_vector_index = 0;
    int int_vector_index = 0;
    int ll_vector_index = 0;
    int float_vector_index = 0;
    int double_vector_index = 0;
    std::unordered_map<int, int> column_to_data_type_index; // <Column Index, Data Type Index>

    // Fill headers
    for (std::pair<int, Field> field : _fields) {
        Field& tmp_field = field.second;
        CARTA::EntryType tmp_data_type;
        GetDataType(tmp_field.datatype, tmp_data_type);

        // Only fill the header that its data type is in our list
        if (tmp_data_type != CARTA::EntryType::UNKNOWN_TYPE) {
            // Only fill the header that its column index is not in the hided column set
            int column_index = field.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                CARTA::CatalogHeader header;
                header.set_name(tmp_field.name);
                header.set_data_type(tmp_data_type);
                header.set_column_index(column_index); // The FIELD index in the VOTable
                header.set_description(tmp_field.description);
                header.set_units(tmp_field.unit);

                // Assign the column data type index and column size
                if (_bool_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = bool_vector_index;
                    ++bool_vector_index;
                    tmp_columns_data->add_bool_column();
                } else if (_string_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = string_vector_index;
                    ++string_vector_index;
                    tmp_columns_data->add_string_column();
                } else if (_int_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = int_vector_index;
                    ++int_vector_index;
                    tmp_columns_data->add_int_column();
                } else if (_ll_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = ll_vector_index;
                    ++ll_vector_index;
                    tmp_columns_data->add_ll_column();
                } else if (_float_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = float_vector_index;
                    ++float_vector_index;
                    tmp_columns_data->add_float_column();
                } else if (_double_vectors.count(column_index)) {
                    column_to_data_type_index[column_index] = double_vector_index;
                    ++double_vector_index;
                    tmp_columns_data->add_double_column();
                }

                // Fill the data type index
                header.set_data_type_index(column_to_data_type_index[column_index]);
            }
        }
    }

    // Get the end index of row
    int total_row_num = GetTableRowNumber();
    if (subset_start_index > total_row_num - 1) {
        std::cerr << "Start row index is out of range!" << std::endl;
        return;
    }
    if (subset_data_size < 0) {
        std::cerr << "Subset data size is negative!" << std::endl;
        return;
    }
    int subset_end_index = subset_start_index + subset_data_size - 1;
    if (subset_end_index > total_row_num - 1) {
        subset_end_index = total_row_num - 1;
    }

    // Loop the table row data
    int row_size = subset_end_index - subset_start_index + 1;
    float last_progress = _check_filter_progress_interval;
    int x_axis_count = 0;
    int y_axis_count = 0;
    for (int row = subset_start_index; row <= subset_end_index; ++row) {
        // Loop the table column
        bool fill(true);
        bool within_image_bounds(true);

        // Apply the image bounds and determine whether to fill the row data
        for (std::pair<int, Field> field : _fields) {
            if (catalog_image_bounds.x_column_name() == field.second.name) {
                ++x_axis_count;
                if ((x_axis_count < image_bounds.x_min()) || (x_axis_count > image_bounds.x_max())) {
                    within_image_bounds = false;
                }
            }
            if (catalog_image_bounds.y_column_name() == field.second.name) {
                ++y_axis_count;
                if ((y_axis_count < image_bounds.y_min()) || (y_axis_count > image_bounds.y_max())) {
                    within_image_bounds = false;
                }
            }
        }

        if (!within_image_bounds) { // Do not do the following process to fill the row data
            continue;
        }

        // Apply the filter and determine whether to fill the row data
        for (int i = 0; i < filter_request.filter_configs_size(); ++i) {
            auto filter = filter_request.filter_configs(i);
            if (!fill) { // Break the loop once the "fill" boolean becomes false
                break;
            }
            for (std::pair<int, Field> field : _fields) {
                if (filter.column_name() == field.second.name) {
                    int column_index = field.first;
                    if (_bool_vectors.count(column_index)) {
                        bool tmp_value = _bool_vectors[column_index][row];
                        if (!BoolFilter(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    } else if (_string_vectors.count(column_index)) {
                        std::string tmp_value = _string_vectors[column_index][row];
                        if (!StringFilter(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    } else if (_int_vectors.count(column_index)) {
                        int tmp_value = _int_vectors[column_index][row];
                        if (!NumericFilter<int>(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    } else if (_ll_vectors.count(column_index)) {
                        long long tmp_value = _ll_vectors[column_index][row];
                        if (!NumericFilter<long long>(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    } else if (_float_vectors.count(column_index)) {
                        float tmp_value = _float_vectors[column_index][row];
                        if (!NumericFilter<float>(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    } else if (_double_vectors.count(column_index)) {
                        double tmp_value = _double_vectors[column_index][row];
                        if (!NumericFilter<double>(filter, tmp_value)) {
                            fill = false;
                            break; // Break the loop once the row data does not pass the filter
                        }
                    }
                }
            }
        }

        if (!fill) { // Do not do the following process to fill the row data
            continue;
        }

        // Fill the row data
        for (std::pair<int, std::vector<bool>> bool_vector : _bool_vectors) {
            int column_index = bool_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->bool_column_size());
                auto bool_column = tmp_columns_data->mutable_bool_column(data_type_index);
                bool_column->add_bool_column(bool_vector.second[row]);
            }
        }
        for (std::pair<int, std::vector<std::string>> string_vector : _string_vectors) {
            int column_index = string_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->string_column_size());
                auto string_column = tmp_columns_data->mutable_string_column(data_type_index);
                string_column->add_string_column(string_vector.second[row]);
            }
        }
        for (std::pair<int, std::vector<int>> int_vector : _int_vectors) {
            int column_index = int_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->int_column_size());
                auto int_column = tmp_columns_data->mutable_int_column(data_type_index);
                int_column->add_int_column(int_vector.second[row]);
            }
        }
        for (std::pair<int, std::vector<long long>> ll_vector : _ll_vectors) {
            int column_index = ll_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->ll_column_size());
                auto ll_column = tmp_columns_data->mutable_ll_column(data_type_index);
                ll_column->add_ll_column(ll_vector.second[row]);
            }
        }
        for (std::pair<int, std::vector<float>> float_vector : _float_vectors) {
            int column_index = float_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->float_column_size());
                auto float_column = tmp_columns_data->mutable_float_column(data_type_index);
                float_column->add_float_column(float_vector.second[row]);
            }
        }
        for (std::pair<int, std::vector<double>> double_vector : _double_vectors) {
            int column_index = double_vector.first;
            if (hided_column_indices.find(column_index) == hided_column_indices.end()) {
                int data_type_index = column_to_data_type_index[column_index];
                assert(data_type_index < tmp_columns_data->double_column_size());
                auto double_column = tmp_columns_data->mutable_double_column(data_type_index);
                double_column->add_double_column(double_vector.second[row]);
            }
        }

        // Calculate the progress
        float progress = (float)(row - subset_start_index + 1) / (float)row_size;

        if ((progress > last_progress) || (progress >= 1.0)) {
            last_progress = progress + _check_filter_progress_interval;
            filter_response.set_progress(progress);

            // Send partial results by the callback function
            partial_results_callback(filter_response);
        }
    }
}

size_t VOTableCarrier::GetTableRowNumber() {
    UpdateNumOfTableRows();
    return _num_of_rows;
}

void VOTableCarrier::GetDataType(std::string data_type, CARTA::EntryType& catalog_data_type) {
    if (data_type == "boolean") {
        catalog_data_type = CARTA::EntryType::BOOL;
    } else if (data_type == "char") {
        catalog_data_type = CARTA::EntryType::STRING;
    } else if (data_type == "short" || data_type == "int") {
        catalog_data_type = CARTA::EntryType::INT;
    } else if (data_type == "long") {
        catalog_data_type = CARTA::EntryType::LONGLONG;
    } else if (data_type == "float") {
        catalog_data_type = CARTA::EntryType::FLOAT;
    } else if (data_type == "double") {
        catalog_data_type = CARTA::EntryType::DOUBLE;
    } else {
        catalog_data_type = CARTA::EntryType::UNKNOWN_TYPE;
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

bool VOTableCarrier::StringFilter(CARTA::FilterConfig filter, std::string value) {
    std::string sub_string = filter.sub_string();
    if (value.find(sub_string) != std::string::npos) {
        return true;
    }
    return false;
}

template <typename T>
bool VOTableCarrier::NumericFilter(CARTA::FilterConfig filter, T value) {
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