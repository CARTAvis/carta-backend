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
