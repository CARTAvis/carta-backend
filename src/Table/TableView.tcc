/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef VOTABLE_TEST__TABLEVIEW_TCC_
#define VOTABLE_TEST__TABLEVIEW_TCC_

#include "TableView.h"

namespace carta {

template <class T>
std::vector<T> TableView::Values(const Column* column, int64_t start, int64_t end) const {
    auto data_column = DataColumn<T>::TryCast(column);
    if (!data_column || data_column->entries.empty()) {
        return std::vector<T>();
    }
    return data_column->GetColumnData(_is_subset, _subset_indices, start, end);
}

} // namespace carta

#endif // VOTABLE_TEST__TABLEVIEW_TCC_
