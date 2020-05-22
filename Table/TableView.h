#ifndef VOTABLE_TEST__TABLEVIEW_H_
#define VOTABLE_TEST__TABLEVIEW_H_

#include <carta-protobuf/defs.pb.h>

#include "Table.h"

namespace carta {

class Table;

class TableView {
public:
    TableView(const Table& table);
    TableView(const Table& table, const IndexList& index_list, bool ordered = true);

    // Filtering
    bool NumericFilter(const Column* column, ComparisonOperator comparison_operator, double value, double secondary_value = 0.0);
    bool StringFilter(const Column* column, std::string search_string, bool case_insensitive = false);

    bool Invert();
    void Reset();
    bool Combine(const TableView& second);

    // Sorting
    bool SortByColumn(const Column* column, bool ascending = true);
    bool SortByIndex();

    // Retrieving data
    size_t NumRows() const;
    template <class T>
    std::vector<T> Values(const Column* column, int64_t start = -1, int64_t end = -1) const;

    bool FillValues(const Column* column, CARTA::ColumnData& column_data, int64_t start = -1, int64_t end = -1) const;

protected:
    bool _is_subset;
    bool _ordered;
    IndexList _subset_indices;
    const Table& _table;
};
} // namespace carta

#include "TableView.tcc"

#endif // VOTABLE_TEST__TABLEVIEW_H_
