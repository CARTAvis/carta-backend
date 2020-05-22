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

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/defs.pb.h>

namespace carta {

typedef std::vector<int64_t> IndexList;
template <class T>
class DataColumn;

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

    virtual void FillColumnData(CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const {};
    // Factory for constructing a column from a <FIELD> node
    static std::unique_ptr<Column> FromField(const pugi::xml_node& field);
    // Factory for constructing a column from a FITS fle pointer and a given column index. Increments the data_offset value
    static std::unique_ptr<Column> FromFitsPtr(fitsfile* fits_ptr, int column_index, size_t& data_offset);

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
    std::vector<T> GetColumnData(bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const;
    void FillColumnData(CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const override;

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