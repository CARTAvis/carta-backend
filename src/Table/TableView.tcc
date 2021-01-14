#ifndef VOTABLE_TEST__TABLEVIEW_TCC_
#define VOTABLE_TEST__TABLEVIEW_TCC_

#include "TableView.h"

namespace carta {

template<class T>
std::vector<T> TableView::Values(const Column* column, int64_t start, int64_t end) const {
    auto data_column = DataColumn<T>::TryCast(column);
    if (!data_column || data_column->entries.empty()) {
        return std::vector<T>();
    }
    return data_column->GetColumnData(_is_subset, _subset_indices, start, end);
}

}

#endif // VOTABLE_TEST__TABLEVIEW_TCC_
