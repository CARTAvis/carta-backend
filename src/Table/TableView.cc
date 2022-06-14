/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "TableView.h"

#include <algorithm>
#include <numeric>

#include "Table.h"
#include "ThreadingManager/ThreadingManager.h"

namespace carta {

TableView::TableView(const Table& table) : _table(table) {
    _is_subset = false;
    _ordered = true;
}

TableView::TableView(const Table& table, const IndexList& index_list, bool ordered)
    : _table(table), _subset_indices(index_list), _ordered(ordered) {
    _is_subset = true;
}

bool TableView::NumericFilter(const Column* column, CARTA::ComparisonOperator comparison_operator, double value, double secondary_value) {
    if (!column) {
        return false;
    }

    // Only filter for arithmetic types
    if (column->data_type == CARTA::UnsupportedType || column->data_type == CARTA::String) {
        return false;
    }

    column->FilterIndices(_subset_indices, _is_subset, comparison_operator, value, secondary_value);
    size_t num_entries = column->NumEntries();

    if (_subset_indices.size() == num_entries) {
        _subset_indices.clear();
        _is_subset = false;
    } else {
        _is_subset = true;
    }

    return true;
}

bool TableView::StringFilter(const Column* column, string search_string, bool case_insensitive) {
    IndexList matching_indices;

    auto string_column = DataColumn<string>::TryCast(column);
    if (!string_column) {
        _is_subset = true;
        return false;
    }
    size_t num_entries = string_column->entries.size();

    if (case_insensitive) {
        // If case-insensitive, must transform strings to lower-case while iterating
        transform(search_string.begin(), search_string.end(), search_string.begin(), ::tolower);
        if (_is_subset) {
            for (auto i : _subset_indices) {
                // Skip invalid entries
                if (i < 0 || i >= num_entries) {
                    continue;
                }
                auto val = string_column->entries[i];
                transform(val.begin(), val.end(), val.begin(), ::tolower);
                if (val.find(search_string) != string::npos) {
                    matching_indices.push_back(i);
                }
            }
        } else {
            for (auto i = 0; i < num_entries; i++) {
                auto val = string_column->entries[i];
                transform(val.begin(), val.end(), val.begin(), ::tolower);
                if (val.find(search_string) != string::npos) {
                    matching_indices.push_back(i);
                }
            }
        }
    } else {
        if (_is_subset) {
            for (auto i : _subset_indices) {
                // Skip invalid entries
                if (i < 0 || i >= num_entries) {
                    continue;
                }
                auto& val = string_column->entries[i];
                if (val.find(search_string) != string::npos) {
                    matching_indices.push_back(i);
                }
            }
        } else {
            for (auto i = 0; i < num_entries; i++) {
                auto& val = string_column->entries[i];
                if (val.find(search_string) != string::npos) {
                    matching_indices.push_back(i);
                }
            }
        }
    }

    if (matching_indices.size() == num_entries) {
        _subset_indices.clear();
        _is_subset = false;
    } else {
        _is_subset = true;
        _subset_indices = matching_indices;
    }
    return true;
}

bool TableView::Invert() {
    IndexList inverted_indices;
    auto total_row_count = _table.NumRows();

    if (_is_subset) {
        if (_subset_indices.empty()) {
            // Invert of NONE is ALL, so new view is not a subset
            _is_subset = false;
            _subset_indices.clear();
        } else if (_ordered) {
            // Can only invert if subset is ordered
            auto inverted_row_count = total_row_count - _subset_indices.size();
            inverted_indices.reserve(inverted_row_count);
            auto end = _subset_indices.end();
            auto it = _subset_indices.begin();
            auto next_val = *it;
            for (auto i = 0; i < total_row_count; i++) {
                // index is in the existing set
                if (i == next_val) {
                    it++;

                    if (it == end) {
                        next_val = -1;
                    } else {
                        next_val = *it;
                    }
                } else {
                    inverted_indices.push_back(i);
                }
            }
            _subset_indices = inverted_indices;
        } else {
            return false;
        }
    } else {
        // Inverse of ALL is NONE
        _is_subset = true;
        _subset_indices.clear();
    }
    return true;
}

void TableView::Reset() {
    _is_subset = false;
    _subset_indices.clear();
    _ordered = true;
}

bool TableView::Combine(const TableView& second) {
    // If the views point to different tables, the combined table is not valid
    if (&_table != &second._table) {
        return false;
    }
    // If either table is not a subset, the combined table is not a subset
    if (!(_is_subset && second._is_subset)) {
        _is_subset = false;
        return true;
    }

    // If either table has unordered indices, they cannot be combined
    if (!(_ordered && second._ordered)) {
        return false;
    }

    IndexList combined_indices;
    set_union(_subset_indices.begin(), _subset_indices.end(), second._subset_indices.begin(), second._subset_indices.end(),
        back_inserter(combined_indices));
    if (combined_indices.size() == _table.NumRows()) {
        _subset_indices.clear();
        _is_subset = false;
    } else {
        _is_subset = true;
        _subset_indices = combined_indices;
    }

    return true;
}

bool TableView::FillValues(const Column* column, CARTA::ColumnData& column_data, int64_t start, int64_t end) const {
    if (!column) {
        return false;
    }

    column->FillColumnData(column_data, _is_subset, _subset_indices, start, end);
    return true;
}

bool TableView::SortByColumn(const Column* column, bool ascending) {
    if (!column) {
        return false;
    }

    if (column->data_type == CARTA::UnsupportedType) {
        return false;
    }

    // If we're sorting an entire column, we first need to populate the indices
    if (!_is_subset) {
        _subset_indices.resize(_table.NumRows());
        std::iota(_subset_indices.begin(), _subset_indices.end(), 0);
        _is_subset = true;
    }
    column->SortIndices(_subset_indices, ascending);

    // After sorting by a specific column, the table view is no longer ordered by index
    _ordered = false;
    return true;
}

bool TableView::SortByIndex() {
    if (!_ordered) {
        parallel_sort(_subset_indices.begin(), _subset_indices.end());
    }
    _ordered = true;
    return true;
}

size_t TableView::NumRows() const {
    if (_is_subset) {
        return _subset_indices.size();
    }
    return _table.NumRows();
}
} // namespace carta
