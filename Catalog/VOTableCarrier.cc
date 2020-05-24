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
        if (tmp_field.datatype != CARTA::UnsupportedType) { // Only fill the header that its data type is in our list
            auto header = file_info_response.add_headers();
            header->set_name(tmp_field.name);
            header->set_data_type(tmp_field.datatype);
            header->set_column_index(field.first); // The FIELD index in the VOTable
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

size_t VOTableCarrier::GetTableRowNumber() {
    UpdateNumOfTableRows();
    return _num_of_rows;
}

CARTA::ColumnType VOTableCarrier::GetDataType(const std::string& data_type) {
    if (data_type == "boolean") {
        return CARTA::Bool;
    } else if (data_type == "char") {
        return CARTA::String;
    } else if (data_type == "short" || data_type == "int") {
        return CARTA::Int32;
    } else if (data_type == "long") {
        return CARTA::Int64;
    } else if (data_type == "float") {
        return CARTA::Float;
    } else if (data_type == "double") {
        return CARTA::Double;
    } else {
        return CARTA::UnsupportedType;
    }
}

bool VOTableCarrier::IsValid() {
    // Empty column header is identified as a NOT valid VOTable file
    return (!_fields.empty());
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
    //    // Check file_id
    //    if (_filter_request.file_id() != filter_request.file_id()) {
    //        return false;
    //    }
    //    // Check hided_headers
    //    if (_filter_request.hided_headers_size() != filter_request.hided_headers_size()) {
    //        return false;
    //    } else {
    //        for (int i = 0; i < filter_request.hided_headers_size(); ++i) {
    //            if (_filter_request.hided_headers(i) != filter_request.hided_headers(i)) {
    //                return false;
    //            }
    //        }
    //    }
    //    // Check filter_configs
    //    if (_filter_request.filter_configs_size() > 0 || filter_request.filter_configs_size() > 0) {
    //        if (_filter_request.filter_configs_size() != filter_request.filter_configs_size()) {
    //            return false;
    //        } else {
    //            for (int i = 0; i < filter_request.filter_configs_size(); ++i) {
    //                auto m_filter_configs = _filter_request.filter_configs(i);
    //                auto filter_configs = filter_request.filter_configs(i);
    //                if ((!m_filter_configs.column_name().empty() || !filter_configs.column_name().empty()) &&
    //                    (m_filter_configs.column_name() != filter_configs.column_name())) {
    //                    return false;
    //                }
    //                if (m_filter_configs.comparison_operator() != filter_configs.comparison_operator()) {
    //                    return false;
    //                }
    //                if (m_filter_configs.min() != filter_configs.min()) {
    //                    return false;
    //                }
    //                if (m_filter_configs.max() != filter_configs.max()) {
    //                    return false;
    //                }
    //                if ((!m_filter_configs.sub_string().empty() || !filter_configs.sub_string().empty()) &&
    //                    (m_filter_configs.sub_string() != filter_configs.sub_string())) {
    //                    return false;
    //                }
    //            }
    //        }
    //    }
    //    // Check image_bounds
    //    auto _image_bounds = _filter_request.image_bounds();
    //    auto image_bounds = filter_request.image_bounds();
    //    if ((!_image_bounds.x_column_name().empty() || !image_bounds.x_column_name().empty()) &&
    //        (_image_bounds.x_column_name() != image_bounds.x_column_name())) {
    //        return false;
    //    }
    //    if ((!_image_bounds.y_column_name().empty() || !image_bounds.y_column_name().empty()) &&
    //        (_image_bounds.y_column_name() != image_bounds.y_column_name())) {
    //        return false;
    //    }
    //    if (_image_bounds.image_bounds().x_min() != image_bounds.image_bounds().x_min()) {
    //        return false;
    //    }
    //    if (_image_bounds.image_bounds().x_max() != image_bounds.image_bounds().x_max()) {
    //        return false;
    //    }
    //    if (_image_bounds.image_bounds().y_min() != image_bounds.image_bounds().y_min()) {
    //        return false;
    //    }
    //    if (_image_bounds.image_bounds().y_max() != image_bounds.image_bounds().y_max()) {
    //        return false;
    //    }
    //    // Check image_file_id
    //    if (_filter_request.image_file_id() != filter_request.image_file_id()) {
    //        return false;
    //    }
    //    // Check region_id
    //    if (_filter_request.region_id() != filter_request.region_id()) {
    //        return false;
    //    }
    //    // Check sort_column and sorting_type
    //    if (!_filter_request.sort_column().empty() || !filter_request.sort_column().empty()) {
    //        if (_filter_request.sort_column() != filter_request.sort_column()) {
    //            return false;
    //        } else {
    //            if (_filter_request.sorting_type() != filter_request.sorting_type()) {
    //                return false;
    //            }
    //        }
    //    }

    return true;
}