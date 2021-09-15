/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "String.h"

void SplitString(string& input, char delim, vector<string>& parts) {
    // util to split input string into parts by delimiter
    parts.clear();
    stringstream ss(input);
    string item;
    while (getline(ss, item, delim)) {
        if (!item.empty()) {
            if (item.back() == '\r') {
                item.pop_back();
            }
            parts.push_back(item);
        }
    }
}
