#pragma once

#include <string>
#include <chrono>
#include <fmt/format.h>
#include <fmt/ostream.h>

void log(const std::string& uuid, const std::string& logMessage);

template<typename... Args>
inline void log(const std::string& uuid, const char* templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}

template<typename... Args>
inline void log(const std::string& uuid, const std::string& templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}
