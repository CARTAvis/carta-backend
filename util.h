#pragma once
#include <string>

#include <string>
#include <chrono>
#include <fmt/format.h>

using namespace std;

void log(const string& uuid, const string& logMessage) {
    // Shorten uuids a bit for brevity
    auto uuidString = uuid;
    auto lastHash = uuidString.find_last_of('-');
    if (lastHash != string::npos) {
        uuidString = uuidString.substr(lastHash + 1);
    }
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);

    fmt::print("Session {} ({}): {}\n", uuidString, timeString, logMessage);
}

template<typename... Args>
void log(const string& uuid, const char* templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}

template<typename... Args>
void log(const string& uuid, const std::string& templateString, Args... args) {
    log(uuid, fmt::format(templateString, args...));
}
