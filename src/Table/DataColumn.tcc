/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef VOTABLE_TEST__DATACOLUMN_TCC_
#define VOTABLE_TEST__DATACOLUMN_TCC_

#include "Columns.h"

#include <vector>

#include "ThreadingManager/ThreadingManager.h"

namespace carta {

template <class T>
T clamp(T val, const T& min_val, const T& max_val) {
    if (val < min_val) {
        val = min_val;
    }
    if (val > max_val) {
        val = max_val;
    }
    return val;
}

template <class T>
DataColumn<T>::DataColumn(const std::string& name_chr, bool is_logical_field) : Column(name_chr) {
    _is_logical_field = is_logical_field;
    // Assign type based on template type
    if constexpr (std::is_same_v<T, std::string>) {
        data_type = CARTA::String;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        if (_is_logical_field) {
            data_type = CARTA::Bool;
        } else {
            data_type = CARTA::Uint8;
        }
    } else if constexpr (std::is_same_v<T, int8_t>) {
        data_type = CARTA::Int8;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        data_type = CARTA::Uint16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        data_type = CARTA::Int16;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        data_type = CARTA::Uint32;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        data_type = CARTA::Int32;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        data_type = CARTA::Uint64;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        data_type = CARTA::Int64;
    } else if constexpr (std::is_same_v<T, float>) {
        data_type = CARTA::Float;
    } else if constexpr (std::is_same_v<T, double>) {
        data_type = CARTA::Double;
    } else {
        data_type = CARTA::UnsupportedType;
    }

    if (data_type == CARTA::UnsupportedType) {
        data_type_size = 0;
    } else if (data_type == CARTA::String) {
        data_type_size = 1;
    } else {
        data_type_size = sizeof(T);
    }
}

template <class T>
T DataColumn<T>::FromText(const pugi::xml_text& text) {
    // Parse properly based on template type or traits
    if constexpr (std::is_same_v<T, std::string>) {
        return text.as_string();
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        if (_is_logical_field) {
            return text.as_bool() ? 1 : 0;
        }
        return text.as_int(0);
    } else if constexpr (std::is_same_v<T, double>) {
        return text.as_double(std::numeric_limits<T>::quiet_NaN());
    } else if constexpr (std::is_floating_point_v<T>) {
        return text.as_float(std::numeric_limits<T>::quiet_NaN());
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return text.as_llong(0);
    } else if constexpr (std::is_arithmetic_v<T>) {
        return text.as_int(0);
    } else {
        return T();
    }
}

template <class T>
void DataColumn<T>::SetFromText(const pugi::xml_text& text, size_t index) {
    entries[index] = FromText(text);
}

template <class T>
void DataColumn<T>::SetFromValue(const T value, size_t index) {
    entries[index] = value;
}

template <class T>
void DataColumn<T>::SetEmpty(size_t index) {
    if constexpr (std::numeric_limits<T>::has_quiet_NaN) {
        entries[index] = std::numeric_limits<T>::quiet_NaN();
    } else {
        entries[index] = T();
    }
}

template <class T>
void DataColumn<T>::FillFromBuffer(const uint8_t* ptr, int num_rows, size_t stride) {
    // Shifts by the column's offset
    ptr += data_offset;
    T* val_ptr = entries.data();

    if (!stride || !data_type_size || num_rows > entries.size()) {
        return;
    }

    // Convert from big-endian to little-endian if the data type holds multiple bytes.
    // The constexpr qualifier means that the if statements will be evaluated at compile-time to avoid branching
    for (auto i = 0; i < num_rows; i++) {
        if constexpr (sizeof(T) == 2) {
            uint16_t temp_val;
            memcpy(&temp_val, ptr + stride * i, sizeof(T));
            temp_val = __builtin_bswap16(temp_val);
            entries[i] = *((T*)&temp_val);
        } else if constexpr (sizeof(T) == 4) {
            uint32_t temp_val;
            memcpy(&temp_val, ptr + stride * i, sizeof(T));
            temp_val = __builtin_bswap32(temp_val);
            entries[i] = *((T*)&temp_val);
        } else if constexpr (sizeof(T) == 8) {
            uint64_t temp_val;
            memcpy(&temp_val, ptr + stride * i, sizeof(T));
            temp_val = __builtin_bswap64(temp_val);
            entries[i] = *((T*)&temp_val);
        } else {
            memcpy(val_ptr + i, ptr + stride * i, sizeof(T));
        }
    }
}

template <class T>
void DataColumn<T>::Resize(size_t capacity) {
    entries.resize(capacity);
}

template <class T>
size_t DataColumn<T>::NumEntries() const {
    return entries.size();
}

template <class T>
void DataColumn<T>::SortIndices(IndexList& indices, bool ascending) const {
    if (indices.empty() || entries.empty()) {
        return;
    }

    // Perform ascending or descending sort
    if (ascending) {
        parallel_sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) {
            auto val_a = entries[a];
            auto val_b = entries[b];
            if (std::isnan(val_a)) {
                return false;
            } else if (std::isnan(val_b)) {
                return true;
            } else {
                return val_a < val_b;
            }
        });
    } else {
        parallel_sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) {
            auto val_a = entries[a];
            auto val_b = entries[b];
            if (std::isnan(val_a)) {
                return false;
            } else if (std::isnan(val_b)) {
                return true;
            } else {
                return val_a > val_b;
            }
        });
    }
}

template <class T>
void DataColumn<T>::FilterIndices(IndexList& existing_indices, bool is_subset, CARTA::ComparisonOperator comparison_operator, double value,
    double secondary_value) const {
    // only apply to template types that are arithmetic
    if constexpr (std::is_arithmetic_v<T>) {
        T typed_value = value;
        T typed_secondary_value = secondary_value;

        IndexList matching_indices;
        size_t num_entries = entries.size();

        if (is_subset) {
            for (const auto& i : existing_indices) {
                // Skip invalid entries
                if (i < 0 || i >= num_entries) {
                    continue;
                }
                T val = entries[i];
                bool filter_pass = (comparison_operator == CARTA::Equal && val == typed_value) ||
                                   (comparison_operator == CARTA::NotEqual && val != typed_value) ||
                                   (comparison_operator == CARTA::Lesser && val < typed_value) ||
                                   (comparison_operator == CARTA::Greater && val > typed_value) ||
                                   (comparison_operator == CARTA::LessorOrEqual && val <= typed_value) ||
                                   (comparison_operator == CARTA::GreaterOrEqual && val >= typed_value) ||
                                   (comparison_operator == CARTA::RangeClosed && val >= typed_value && val <= typed_secondary_value) ||
                                   (comparison_operator == CARTA::RangeOpen && val > typed_value && val < typed_secondary_value);
                if (filter_pass) {
                    matching_indices.push_back(i);
                }
            }
        } else {
            for (auto i = 0; i < num_entries; i++) {
                T val = entries[i];
                bool filter_pass = (comparison_operator == CARTA::Equal && val == typed_value) ||
                                   (comparison_operator == CARTA::NotEqual && val != typed_value) ||
                                   (comparison_operator == CARTA::Lesser && val < typed_value) ||
                                   (comparison_operator == CARTA::Greater && val > typed_value) ||
                                   (comparison_operator == CARTA::LessorOrEqual && val <= typed_value) ||
                                   (comparison_operator == CARTA::GreaterOrEqual && val >= typed_value) ||
                                   (comparison_operator == CARTA::RangeClosed && val >= typed_value && val <= typed_secondary_value) ||
                                   (comparison_operator == CARTA::RangeOpen && val > typed_value && val < typed_secondary_value);
                if (filter_pass) {
                    matching_indices.push_back(i);
                }
            }
        }
        existing_indices.swap(matching_indices);
    }
}

template <class T>
std::vector<T> DataColumn<T>::GetColumnData(bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const {
    if (fill_subset) {
        int64_t N = indices.size();
        int64_t begin_index = clamp(start, (int64_t)0, N);
        if (end < 0) {
            end = indices.size();
        }
        int64_t end_index = clamp(end, begin_index, N);

        auto begin_it = indices.begin() + begin_index;
        auto end_it = indices.begin() + end_index;
        std::vector<T> values;
        values.reserve(std::distance(begin_it, end_it));
        for (auto it = begin_it; it != end_it; it++) {
            values.push_back(entries[*it]);
        }
        return values;
    } else {
        int64_t N = entries.size();
        int64_t begin_index = clamp(start, (int64_t)0, N);
        if (end < 0) {
            end = N;
        }
        int64_t end_index = clamp(end, begin_index, N);

        auto begin_it = entries.begin() + begin_index;
        auto end_it = entries.begin() + end_index;
        return std::vector<T>(begin_it, end_it);
    }
}

template <class T>
void DataColumn<T>::FillColumnData(
    CARTA::ColumnData& column_data, bool fill_subset, const IndexList& indices, int64_t start, int64_t end) const {
    column_data.set_data_type(data_type);
    auto values = GetColumnData(fill_subset, indices, start, end);
    column_data.set_binary_data(values.data(), values.size() * sizeof(T));
}

} // namespace carta

#endif // VOTABLE_TEST__DATACOLUMN_TCC_
