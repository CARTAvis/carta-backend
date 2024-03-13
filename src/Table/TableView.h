/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_TABLE_TABLEVIEW_H_
#define CARTA_SRC_TABLE_TABLEVIEW_H_

#include <carta-protobuf/defs.pb.h>

#include "Table.h"

namespace carta {

class Table;

class TableView {
public:
    TableView(const Table& table);
    TableView(const Table& table, const IndexList& index_list, bool ordered = true);

    // Filtering
    bool NumericFilter(const Column* column, CARTA::ComparisonOperator comparison_operator, double value, double secondary_value = 0.0);
    bool StringFilter(const Column* column, std::string search_string, bool case_insensitive = false);

    bool Invert();
    void Reset();
    bool Combine(const TableView& second);

    // Sorting
    bool SortByColumn(const Column* column, bool ascending = true);
    bool SortByIndex();

    // Retrieving data
    size_t NumRows() const;
    const Table& GetTable() const {
        return _table;
    }
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

#endif // CARTA_SRC_TABLE_TABLEVIEW_H_
