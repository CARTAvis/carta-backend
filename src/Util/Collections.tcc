/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_COLLECTIONS_TCC_
#define CARTA_SRC_UTIL_COLLECTIONS_TCC_

#include <map>
#include <unordered_map>

template <typename InMapType, typename OutMapType>
OutMapType InvertedMap(const InMapType& in_map) {
    OutMapType out_map;
    for (const auto& [key, value] : in_map) {
        out_map[value] = key;
    }
    return out_map;
}
#endif // CARTA_SRC_UTIL_COLLECTIONS_TCC_
