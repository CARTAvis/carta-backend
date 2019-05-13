#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <chrono>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/File.h>

void Log(uint32_t id, const std::string& log_message);

template <typename... Args>
inline void Log(uint32_t id, const char* template_string, Args... args) {
    Log(id, fmt::format(template_string, args...));
}

template <typename... Args>
inline void Log(uint32_t id, const std::string& template_string, Args... args) {
    Log(id, fmt::format(template_string, args...));
}

void ReadPermissions(const std::string& filename, std::unordered_map<std::string, std::vector<std::string>>& permissions_map);
bool CheckRootBaseFolders(std::string& root, std::string& base);

#endif // CARTA_BACKEND__UTIL_H_