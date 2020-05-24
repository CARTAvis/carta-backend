#ifndef CARTA_BACKEND__VOTABLECARRIER_H_
#define CARTA_BACKEND__VOTABLECARRIER_H_

#include <tbb/atomic.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "VOTableController.h"

namespace catalog {

class VOTableCarrier {
    // For the element <COOSYS> and its attributes
    struct Coosys {
        std::string id;
        std::string equinox;
        std::string epoch;
        std::string system;
    };

    // For the element <FIELD> and its attributes
    struct Field {
        std::string name;
        std::string id;
        CARTA::ColumnType datatype;
        std::string arraysize;
        std::string width;
        std::string precision;
        std::string xtype;
        std::string unit;
        std::string ucd;
        std::string utype;
        std::string ref;
        std::string type;
        std::string description;
    };

public:
    VOTableCarrier();
    ~VOTableCarrier(){};

    void SetFileName(std::string file_path_name);
    void FillVOTableAttributes(std::string name, std::string version);
    void FillFileDescription(std::string description);
    void FillCoosysAttributes(int count, std::string name, std::string value);
    void FillFieldAttributes(int count, std::string name, std::string value);
    void UpdateNumOfTableRows();
    std::string GetFileDescription();
    void GetHeaders(CARTA::CatalogFileInfoResponse& file_info_response);
    void GetCooosys(CARTA::CatalogFileInfo* file_info_response);
    size_t GetTableRowNumber();
    static CARTA::ColumnType GetDataType(const std::string& data_type);
    bool IsValid();
    void DisconnectCalled();

private:
    bool IsSameFilterRequest(const CARTA::CatalogFilterRequest& filter_request);
    void SetConnectionFlag(bool connected);

    std::string _filename;
    std::string _directory;
    std::string _file_description = "";
    std::string _votable_version = "";       // VOTable version, "" means this is not the VOTable XML file.
    std::unordered_map<int, Coosys> _coosys; // Unordered map for the element <COOSYS>: <COOSYS count (Column Index), COOSYS attributes>
    std::unordered_map<int, Field> _fields;  // Unordered map for the element <FIELD>: <FIELD count (Column Index), FIELD attributes>
    size_t _num_of_rows = 0;                 // Number of table rows
    std::vector<size_t> _row_indexes;        // [0, 1, 2, ...] for primitive data without sorting
    std::string _sort_column;                // Name of the column to sort

    // Unordered map for table columns: <Column Index, Column Vector>
    std::unordered_map<int, std::vector<bool>> _bool_vectors;          // For the column with datatype = "boolean"
    std::unordered_map<int, std::vector<std::string>> _string_vectors; // For the column with datatype = "char"
    std::unordered_map<int, std::vector<int>> _int_vectors;            // For the column with datdtype = "int"
    std::unordered_map<int, std::vector<long long>> _ll_vectors;       // For the column with datdtype = "long long"
    std::unordered_map<int, std::vector<double>> _double_vectors;      // For the column with datdtype = "double"
    std::unordered_map<int, std::vector<float>> _float_vectors;        // For the column with datdtype = "float"

    std::unordered_map<int, int> _column_index_to_data_type_index; // <Column Index, Data Type Index>

    // PS: do not consider the datatypes: "bit", "unsignedByte", "unicodeChar", "floatComplex" and "doubleComplex"

    CARTA::CatalogFilterRequest _filter_request;
    std::vector<int> _fill;       // Bool vector for the filter data, 1 is passed, and 0 is not passed after the filter
    size_t _filter_data_size = 0; // Data size after the filter

    volatile bool _connected = true;
    tbb::atomic<int> _stream_count;
};

} // namespace catalog

#endif // CARTA_BACKEND__VOTABLECARRIER_H_