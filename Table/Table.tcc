#ifndef VOTABLE_TEST__TABLE_TCC_
#define VOTABLE_TEST__TABLE_TCC_

#include "Table.h"

namespace carta {

template<class T>
DataColumn <T>* Table::AddDataColumn(const std::string& name, const std::string& id) {
    if (_column_name_map.count(name)) {
        return nullptr;
    } else {
        auto& column = _columns.emplace_back(std::make_unique<DataColumn<T>>(name));
        _column_name_map[name] = column;
        if (!id.empty()) {
            _column_id_map[id] = column;
        }
        return column;
    }
}
}

#endif