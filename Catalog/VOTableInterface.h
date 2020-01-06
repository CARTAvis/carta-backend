#ifndef CARTA_BACKEND__VOTABLEINTERFACE_H_
#define CARTA_BACKEND__VOTABLEINTERFACE_H_

#include <iostream>
#include <string>
#include <vector>

namespace catalog {

// Enums

enum FileType { VOTable = 0 };

enum ComparisonOperator {
    EqualTo = 0,
    NotEqualTo = 1,
    LessThan = 2,
    GreaterThan = 3,
    LessThanOrEqualTo = 4,
    GreaterThanOrEqualTo = 5,
    BetweenAnd = 6,
    FromTo = 7
};

enum DataType { BOOL = 0, STRING = 1, INT = 2, LONG = 3, FLOAT = 4, DOUBLE = 5, NONE = 6 };

// Structs for sub-message

struct FileInfo {
    std::string filename;
    FileType file_type;
    std::string description;
    void Print() {
        std::cout << "FileInfo:" << std::endl;
        std::cout << "    filename = " << filename << std::endl;
        std::cout << "    file_type = " << file_type << std::endl;
        std::cout << "    description = " << description << std::endl;
    }
};

struct Header {
    std::string column_name;
    DataType data_type;
    int column_index;
    int data_type_index;
    std::string description;
    std::string unit;
    void Print() {
        std::cout << "Header:" << std::endl;
        std::cout << "    column_name = " << column_name << std::endl;
        std::cout << "    data_type = " << data_type << std::endl;
        std::cout << "    column_index = " << column_index << std::endl;
        std::cout << "    data_type_index = " << data_type_index << std::endl;
        std::cout << "    description = " << description << std::endl;
        std::cout << "    unit = " << unit << std::endl;
    }
};

struct ColumnsData {
    std::vector<std::vector<bool>> bool_columns;
    std::vector<std::vector<std::string>> string_columns;
    std::vector<std::vector<int>> int_columns;
    std::vector<std::vector<long>> long_columns;
    std::vector<std::vector<float>> float_columns;
    std::vector<std::vector<double>> double_columns;
    void Print() {
        for (int i = 0; i < bool_columns.size(); ++i) {
            std::cout << "bool_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < bool_columns[i].size(); ++j) {
                std::cout << bool_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
        for (int i = 0; i < string_columns.size(); ++i) {
            std::cout << "string_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < string_columns[i].size(); ++j) {
                std::cout << string_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
        for (int i = 0; i < int_columns.size(); ++i) {
            std::cout << "int_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < int_columns[i].size(); ++j) {
                std::cout << int_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
        for (int i = 0; i < long_columns.size(); ++i) {
            std::cout << "long_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < long_columns[i].size(); ++j) {
                std::cout << long_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
        for (int i = 0; i < float_columns.size(); ++i) {
            std::cout << "float_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < float_columns[i].size(); ++j) {
                std::cout << float_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
        for (int i = 0; i < double_columns.size(); ++i) {
            std::cout << "double_columns[" << i << "]:" << std::endl;
            for (int j = 0; j < double_columns[i].size(); ++j) {
                std::cout << double_columns[i][j] << " | ";
            }
            std::cout << std::endl;
        }
    }
};

struct FilterConfig {
    std::string column_name;
    ComparisonOperator comparison_operator;
    float min;
    float max;
    std::string sub_string;
    DataType data_type;
    void Print() {
        std::cout << "FilterConfig:" << std::endl;
        std::cout << "    column_name = " << column_name << std::endl;
        std::cout << "    comparison_operator = " << comparison_operator << std::endl;
        std::cout << "    min = " << min << std::endl;
        std::cout << "    max = " << max << std::endl;
        std::cout << "    sub_string = " << sub_string << std::endl;
        std::cout << "    data_type = " << data_type << std::endl;
    }
};

struct ImageBounds {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    ImageBounds& operator=(const ImageBounds& other) {
        if (this != &other) {
            x_min = other.x_min;
            x_max = other.x_max;
            y_min = other.y_min;
            y_max = other.y_max;
        }
        return *this;
    }
    void Print() {
        std::cout << "ImageBounds:" << std::endl;
        std::cout << "    x_min = " << x_min << std::endl;
        std::cout << "    x_max = " << x_max << std::endl;
        std::cout << "    y_min = " << y_min << std::endl;
        std::cout << "    y_max = " << y_max << std::endl;
    }
};

// Structs for request messages

struct FileListRequest {
    std::string directory;
    void Print() {
        std::cout << "FileListRequest:" << std::endl;
        std::cout << "    directory = " << directory << std::endl;
    }
};

struct FileInfoRequest {
    std::string directory;
    std::string filename;
    void Print() {
        std::cout << "FileInfoRequest:" << std::endl;
        std::cout << "    directory = " << directory << std::endl;
        std::cout << "    filename = " << filename << std::endl;
    }
};

struct OpenFileRequest {
    std::string directory;
    std::string filename;
    int file_id;
    int preview_data_size;
    void Print() {
        std::cout << "OpenFileRequest:" << std::endl;
        std::cout << "    directory = " << directory << std::endl;
        std::cout << "    filename = " << filename << std::endl;
        std::cout << "    file_id = " << file_id << std::endl;
        std::cout << "    preview_data_size = " << preview_data_size << std::endl;
    }
};

struct CloseFileRequest {
    int file_id;
};

struct FilterRequest {
    int file_id;
    std::vector<std::string> hided_table_headers;
    std::vector<FilterConfig> filter_configs;
    int subset_data_size;
    int subset_start_index;
    ImageBounds image_bounds;
    int region_id;
    void Print() {
        std::cout << "FilterRequest:" << std::endl;
        std::cout << "    file_id = " << file_id << std::endl;
        for (int i = 0; i < hided_table_headers.size(); ++i) {
            std::cout << "    hided_table_headers[" << i << "] = " << hided_table_headers[i] << std::endl;
        }
        for (int i = 0; i < filter_configs.size(); ++i) {
            std::cout << "    filter_configs[" << i << "]:" << std::endl;
            filter_configs[i].Print();
        }
        std::cout << "    subset_data_size = " << subset_data_size << std::endl;
        std::cout << "    subset_start_index = " << subset_start_index << std::endl;
        image_bounds.Print();
        std::cout << "    region_id = " << region_id << std::endl;
    }
};

// Structs for response messages

struct FileListResponse {
    bool success;
    std::string message;
    std::string directory;
    std::string parent;
    std::vector<FileInfo> files;
    std::vector<std::string> subdirectories;
    void Print() {
        std::cout << "FileListResponse:" << std::endl;
        std::cout << "    success = " << success << std::endl;
        std::cout << "    message = " << message << std::endl;
        std::cout << "    directory = " << directory << std::endl;
        std::cout << "    parent = " << parent << std::endl;
        for (int i = 0; i < files.size(); ++i) {
            std::cout << "files[" << i << "]:" << std::endl;
            files[i].Print();
        }
        for (int i = 0; i < subdirectories.size(); ++i) {
            std::cout << "    subdirectories " << i << ": " << subdirectories[i] << std::endl;
        }
    }
};

struct FileInfoResponse {
    bool success;
    std::string message;
    FileInfo file_info;
    int data_size;
    std::vector<Header> headers;
    void Print() {
        std::cout << "FileInfoResponse:" << std::endl;
        std::cout << "    success = " << success << std::endl;
        std::cout << "    message = " << message << std::endl;
        file_info.Print();
        std::cout << "    data_size = " << data_size << std::endl;
        for (int i = 0; i < headers.size(); ++i) {
            std::cout << "Header[" << i << "]:" << std::endl;
            headers[i].Print();
        }
    }
};

struct OpenFileResponse {
    bool success;
    std::string message;
    int file_id;
    FileInfo file_info;
    int data_size;
    std::vector<Header> headers;
    ColumnsData columns_data;
    void Print() {
        std::cout << "OpenFileResponse:" << std::endl;
        std::cout << "    success = " << success << std::endl;
        std::cout << "    message = " << message << std::endl;
        std::cout << "    file_id = " << file_id << std::endl;
        file_info.Print();
        std::cout << "    data_size = " << data_size << std::endl;
        for (int i = 0; i < headers.size(); ++i) {
            std::cout << "Header[" << i << "]:" << std::endl;
            headers[i].Print();
        }
        columns_data.Print();
    }
};

struct FilterResponse {
    int file_id;
    int region_id;
    std::vector<Header> headers;
    ColumnsData columns_data;
    float progress;
    void Print() {
        std::cout << "FilterResponse:" << std::endl;
        std::cout << "    file_id = " << file_id << std::endl;
        std::cout << "    region_id = " << region_id << std::endl;
        for (int i = 0; i < headers.size(); ++i) {
            std::cout << "Header[" << i << "]:" << std::endl;
            headers[i].Print();
        }
        columns_data.Print();
        std::cout << "    progress = " << progress << std::endl;
    }
};

} // namespace catalog

#endif // CARTA_BACKEND__VOTABLEINTERFACE_H_