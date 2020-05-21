#ifndef VOTABLE_TEST__TABLEVIEW_TCC_
#define VOTABLE_TEST__TABLEVIEW_TCC_

namespace carta {

template<class T>
T clamp(T val, const T& min_val, const T& max_val) {
    if (val < min_val) { val = min_val; }
    if (val > max_val) { val = max_val; }
    return val;
}

template<class T>
std::vector<T> TableView::Values(const Column* column, int64_t start, int64_t end) const {
    auto data_column = DataColumn<T>::TryCast(column);
    if (!data_column || data_column->entries.empty()) {
        return std::vector<T>();
    }

    if (_is_subset) {
        int64_t N = _subset_indices.size();
        int64_t begin_index = clamp(start, (int64_t) 0, N);
        if (end < 0) {
            end = _subset_indices.size();
        }
        int64_t end_index = clamp(end, begin_index, N);

        auto begin_it = _subset_indices.begin() + begin_index;
        auto end_it = _subset_indices.begin() + end_index;
        std::vector<T> values;
        values.reserve(std::distance(begin_it, end_it));

        auto& entries = data_column->entries;
        for (auto it = begin_it; it != end_it; it++) {
            values.push_back(entries[*it]);
        }
        return values;
    } else {
        int64_t N = data_column->entries.size();
        int64_t begin_index = clamp(start, (int64_t) 0, N);
        if (end < 0) {
            end = N;
        }
        int64_t end_index = clamp(end, begin_index, N);

        auto begin_it = data_column->entries.begin() + begin_index;
        auto end_it = data_column->entries.begin() + end_index;
        return std::vector<T>(begin_it, end_it);
    }
}

}

#endif // VOTABLE_TEST__TABLEVIEW_TCC_
