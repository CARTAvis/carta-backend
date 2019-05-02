#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <chrono>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/File.h>

using namespace std;

void log(uint32_t id, const string& logMessage);

template <typename... Args>
inline void log(uint32_t id, const char* templateString, Args... args) {
    log(id, fmt::format(templateString, args...));
}

template <typename... Args>
inline void log(uint32_t id, const string& templateString, Args... args) {
    log(id, fmt::format(templateString, args...));
}

void readPermissions(string filename, unordered_map<string, vector<string>>& permissionsMap);
bool checkRootBaseFolders(string& root, string& base);

#endif // CARTA_BACKEND__UTIL_H_