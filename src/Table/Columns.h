/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef VOTABLE_TEST__COLUMNS_H_
#define VOTABLE_TEST__COLUMNS_H_

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <valarray>
#include <vector>

#include <fitsio.h>
#include <fmt/format.h>
#include <pugixml.hpp>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#define DECLARE_UNARY_OPERATION(NAME) static bool NAME(DataColumn<T>* output_column, const DataColumn<T>* a_column);
#define DECLARE_UNARY_OPERATION_ONE_PARAMETER(NAME) static bool NAME(DataColumn<T>* output_column, const DataColumn<T>* a_column, T b);
#define DECLARE_UNARY_OPERATION_TWO_PARAMETERS(NAME) \
    static bool NAME(DataColumn<T>* output_column, const DataColumn<T>* a_column, T b, T c);
#define DECLARE_BINARY_OPERATION(NAME) \
    static bool NAME(DataColumn<T>* output_column, const DataColumn<T>* a_column, const DataColumn<T>* b_column);

namespace carta {

typedef std::vector<int64_t> IndexList;
template <class T>
class DataColumn;

class Column {
public:
    Column(const std::string& name_chr);
    virtual ~Column() = default;
    virtual void SetFromText(const pugi::xml_text& text, size_t index){};
    virtual void SetEmpty(size_t index){};
    virtual void FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride){};
    virtual void Resize(size_t capacity){};
    virtual size_t NumEntries() const {
        return 0;
    }
    virtual void SortIndices(IndexList& indices, bool ascending) const {};
    virtual void FilterIndices(IndexList& existing_indices, bool is_subset, CARTA::ComparisonOperator comparison_operator, double value,
        double secondary_value = 0.0) const {}

    virtual void FillColumnData(
        CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const {};
    // Factory for constructing a column from a <FIELD> node
    static std::unique_ptr<Column> FromField(const pugi::xml_node& field);
    // Factory for constructing a column from a FITS fle pointer and a given column index. Increments the data_offset value
    static std::unique_ptr<Column> FromFitsPtr(fitsfile* fits_ptr, int column_index, size_t& data_offset);
    // Factory for constructing a column from a data(string) vector
    static std::unique_ptr<Column> FromValues(const std::vector<std::string>& values, const std::string& column_name);

    CARTA::ColumnType data_type;
    std::string name;
    std::string id;
    std::string unit;
    std::string ucd;
    std::string description;
    size_t data_type_size;
    size_t data_offset;
};

template <class T>
class DataColumn : public Column {
public:
    std::valarray<T> entries;
    DataColumn(const std::string& name_chr);
    virtual ~DataColumn() = default;
    void SetFromText(const pugi::xml_text& text, size_t index) override;
    void SetFromValue(T value, size_t index);
    void SetEmpty(size_t index) override;
    void FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride) override;
    void Resize(size_t capacity) override;
    size_t NumEntries() const override;
    void SortIndices(IndexList& indices, bool ascending) const override;
    void FilterIndices(IndexList& existing_indices, bool is_subset, CARTA::ComparisonOperator comparison_operator, double value,
        double secondary_value = 0.0) const override;
    std::valarray<T> GetColumnData(bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const;
    void FillColumnData(
        CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const override;

    // Column Operation Declarations
    // output = operation(A)
    DECLARE_UNARY_OPERATION(Sqrt);
    DECLARE_UNARY_OPERATION(Cos);
    DECLARE_UNARY_OPERATION(Sin);
    DECLARE_UNARY_OPERATION(Tan);
    DECLARE_UNARY_OPERATION(Log);
    DECLARE_UNARY_OPERATION(Exp);
    DECLARE_UNARY_OPERATION(Ceil);
    DECLARE_UNARY_OPERATION(Floor);
    DECLARE_UNARY_OPERATION(Round);
    DECLARE_UNARY_OPERATION(Reverse);
    DECLARE_UNARY_OPERATION(First);
    DECLARE_UNARY_OPERATION(Last);
    // output = operation(A, b)
    DECLARE_UNARY_OPERATION_ONE_PARAMETER(Scale);
    DECLARE_UNARY_OPERATION_ONE_PARAMETER(Offset);
    DECLARE_UNARY_OPERATION_ONE_PARAMETER(Pow);
    // output = operation(A, b, c)
    DECLARE_UNARY_OPERATION_TWO_PARAMETERS(Clamp);
    // output = operation(A, B)
    DECLARE_BINARY_OPERATION(Add);
    DECLARE_BINARY_OPERATION(Subtract);
    DECLARE_BINARY_OPERATION(Multiply);
    DECLARE_BINARY_OPERATION(Divide);
    DECLARE_BINARY_OPERATION(Max);
    DECLARE_BINARY_OPERATION(Min);

    static const DataColumn<T>* TryCast(const Column* column) {
        if (!column || column->data_type == CARTA::UnsupportedType) {
            return nullptr;
        }
        return dynamic_cast<const DataColumn<T>*>(column);
    }

protected:
    T FromText(const pugi::xml_text& text);
};
} // namespace carta

#endif // VOTABLE_TEST__COLUMNS_H_