#pragma once

#include <string>
#include <chrono>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/Inputs/Input.h>

using namespace std;

void log(const string& uuid, const string& logMessage);

template<typename... Args>
inline void log(const string& uuid, const char* templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}

template<typename... Args>
inline void log(const string& uuid, const string& templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}

void readPermissions(string filename, unordered_map<string, vector<string>>& permissionsMap);
bool checkRootBaseFolders(string& root, string& base);
