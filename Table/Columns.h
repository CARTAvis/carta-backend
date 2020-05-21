#ifndef VOTABLE_TEST__COLUMNS_H_
#define VOTABLE_TEST__COLUMNS_H_

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <fitsio.h>
#include <fmt/format.h>
#include <pugixml.hpp>

namespace carta {

typedef std::vector<int64_t> IndexList;
template <class T>
class DataColumn;

enum DataType { UNKNOWN_TYPE, STRING, UINT8, INT8, UINT16, INT16, UINT32, INT32, UINT64, INT64, FLOAT, DOUBLE, BOOL };

enum ComparisonOperator {
    EQUAL = 0,
    NOT_EQUAL = 1,
    LESSER = 2,
    GREATER = 3,
    LESSER_OR_EQUAL = 4,
    GREATER_OR_EQUAL = 5,
    RANGE_INCLUSIVE = 6,
    RANGE_EXCLUSIVE = 7
};

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
    virtual void FilterIndices(IndexList& existing_indices, bool is_subset, ComparisonOperator comparison_operator, double value,
        double secondary_value = 0.0) const {}
    virtual std::string Info();

    // Factory for constructing a column from a <FIELD> node
    static std::unique_ptr<Column> FromField(const pugi::xml_node& field);
    static std::unique_ptr<Column> FromFitsPtr(fitsfile* fits_ptr, int column_index, size_t& data_offset);

    DataType data_type;
    std::string name;
    std::string id;
    std::string unit;
    std::string ucd;
    std::string ref;
    std::string description;
    std::string data_type_string;
    size_t data_type_size;
    size_t data_offset;
};

template <class T>
class DataColumn : public Column {
public:
    std::vector<T> entries;
    DataColumn(const std::string& name_chr);
    virtual ~DataColumn() = default;
    void SetFromText(const pugi::xml_text& text, size_t index) override;
    void SetEmpty(size_t index) override;
    void FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride) override;
    void Resize(size_t capacity) override;
    size_t NumEntries() const override;
    void SortIndices(IndexList& indices, bool ascending) const override;
    void FilterIndices(IndexList& existing_indices, bool is_subset, ComparisonOperator comparison_operator, double value,
        double secondary_value = 0.0) const override;

    static const DataColumn<T>* TryCast(const Column* column) {
        if (!column || column->data_type == UNKNOWN_TYPE) {
            return nullptr;
        }
        return dynamic_cast<const DataColumn<T>*>(column);
    }

protected:
    T FromText(const pugi::xml_text& text);
};
} // namespace carta

#endif // VOTABLE_TEST__COLUMNS_H_