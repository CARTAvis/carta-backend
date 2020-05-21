#ifndef VOTABLE_TEST__DATACOLUMN_TCC_
#define VOTABLE_TEST__DATACOLUMN_TCC_

#include "Columns.h"

#include <algorithm>

namespace carta {
template<class T>
DataColumn<T>::DataColumn(const std::string& name_chr): Column(name_chr) {
    // Assign type based on template type
    if constexpr(std::is_same_v<T, std::string>) {
        data_type = STRING;
    } else if constexpr(std::is_same_v<T, uint8_t>) {
        data_type = UINT8;
    } else if constexpr(std::is_same_v<T, int8_t>) {
        data_type = INT8;
    } else if constexpr(std::is_same_v<T, uint16_t>) {
        data_type = UINT16;
    } else if constexpr(std::is_same_v<T, int16_t>) {
        data_type = INT16;
    } else if constexpr(std::is_same_v<T, uint32_t>) {
        data_type = UINT32;
    } else if constexpr(std::is_same_v<T, int32_t>) {
        data_type = INT32;
    } else if constexpr(std::is_same_v<T, uint64_t>) {
        data_type = UINT64;
    } else if constexpr(std::is_same_v<T, int64_t>) {
        data_type = INT64;
    } else if constexpr(std::is_same_v<T, float>) {
        data_type = FLOAT;
    } else if constexpr(std::is_same_v<T, double>) {
        data_type = DOUBLE;
    } else if constexpr(std::is_same_v<T, bool>) {
        data_type = BOOL;
    } else {
        data_type = UNKNOWN_TYPE;
    }

    if (data_type == UNKNOWN_TYPE) {
        data_type_size = 0;
    } else if (data_type == STRING) {
        data_type_size = 1;
    } else {
        data_type_size = sizeof(T);
    }
}

template<class T>
T DataColumn<T>::FromText(const pugi::xml_text& text) {
    // Parse properly based on template type or traits
    if constexpr(std::is_same_v<T, std::string>) {
        return text.as_string();
    } else if constexpr (std::is_same_v<T, double>) {
        return text.as_double(std::numeric_limits<T>::quiet_NaN());
    } else if constexpr (std::is_floating_point_v<T>) {
        return text.as_float(std::numeric_limits<T>::quiet_NaN());
    } else if constexpr(std::is_same_v<T, int64_t>) {
        return text.as_llong(0);
    } else if constexpr(std::is_arithmetic_v<T>) {
        return text.as_int(0);
    } else {
        return T();
    }
}

template<class T>
void DataColumn<T>::SetFromText(const pugi::xml_text& text, size_t index) {
    entries[index] = FromText(text);
}

template<class T>
void DataColumn<T>::SetEmpty(size_t index) {
    if constexpr(std::numeric_limits<T>::has_quiet_NaN) {
        entries[index] = std::numeric_limits<T>::quiet_NaN();
    } else {
        entries[index] = T();
    }
}

template<class T>
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
            entries[i] = *((T*) &temp_val);
        } else if constexpr(sizeof(T) == 4) {
            uint32_t temp_val;
            memcpy(&temp_val, ptr + stride * i, sizeof(T));
            temp_val = __builtin_bswap32(temp_val);
            entries[i] = *((T*) &temp_val);
        } else if constexpr(sizeof(T) == 8) {
            uint64_t temp_val;
            memcpy(&temp_val, ptr + stride * i, sizeof(T));
            temp_val = __builtin_bswap64(temp_val);
            entries[i] = *((T*) &temp_val);
        } else {
            memcpy(val_ptr + i, ptr + stride * i, sizeof(T));
        }
    }
}

template<class T>
void DataColumn<T>::Resize(size_t capacity) {
    entries.resize(capacity);
}

template<class T>
size_t DataColumn<T>::NumEntries() const {
    return entries.size();
}

template<class T>
void DataColumn<T>::SortIndices(IndexList& indices, bool ascending) const {
    if (indices.empty() || entries.empty()) {
        return;
    }

    // Perform ascending or descending sort
    if (ascending) {
        std::sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) {
            return entries[a] < entries[b];
        });
    } else {
        std::sort(indices.begin(), indices.end(), [&](int64_t a, int64_t b) {
            return entries[a] > entries[b];
        });
    }
}

template<class T>
void DataColumn<T>::FilterIndices(IndexList& existing_indices, bool is_subset, ComparisonOperator comparison_operator, double value, double secondary_value) const {
    // only apply to template types that are arithmetic
    if constexpr (std::is_arithmetic_v<T>) {
        T typed_value = value;
        T typed_secondary_value = secondary_value;

        IndexList matching_indices;
        size_t num_entries = entries.size();

        if (is_subset) {
            for (auto i: existing_indices) {
                // Skip invalid entries
                if (i < 0 || i >= num_entries) {
                    continue;
                }
                T val = entries[i];
                bool filter_pass = (comparison_operator == EQUAL && val == typed_value)
                    || (comparison_operator == NOT_EQUAL && val != typed_value)
                    || (comparison_operator == LESSER && val < typed_value)
                    || (comparison_operator == GREATER && val > typed_value)
                    || (comparison_operator == LESSER_OR_EQUAL && val <= typed_value)
                    || (comparison_operator == GREATER_OR_EQUAL && val >= typed_value)
                    || (comparison_operator == RANGE_INCLUSIVE && val >= typed_value && val <= typed_secondary_value)
                    || (comparison_operator == RANGE_EXCLUSIVE && val > typed_value && val < typed_secondary_value);
                if (filter_pass) {
                    matching_indices.push_back(i);
                }
            }
        } else {
            for (auto i = 0; i < num_entries; i++) {
                T val = entries[i];
                bool filter_pass = (comparison_operator == EQUAL && val == typed_value)
                    || (comparison_operator == NOT_EQUAL && val != typed_value)
                    || (comparison_operator == LESSER && val < typed_value)
                    || (comparison_operator == GREATER && val > typed_value)
                    || (comparison_operator == LESSER_OR_EQUAL && val <= typed_value)
                    || (comparison_operator == GREATER_OR_EQUAL && val >= typed_value)
                    || (comparison_operator == RANGE_INCLUSIVE && val >= typed_value && val <= typed_secondary_value)
                    || (comparison_operator == RANGE_EXCLUSIVE && val > typed_value && val < typed_secondary_value);
                if (filter_pass) {
                    matching_indices.push_back(i);
                }
            }
        }
        existing_indices.swap(matching_indices);
    }
}
}

#endif //VOTABLE_TEST__DATACOLUMN_TCC_
