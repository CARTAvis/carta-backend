#include "util.h"

void log(const std::string& uuid, const std::string& logMessage) {
    // Shorten uuids a bit for brevity
    auto uuidString = uuid;
    auto lastHash = uuidString.find_last_of('-');
    if (lastHash != std::string::npos) {
        uuidString = uuidString.substr(lastHash + 1);
    }
    time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);

    fmt::print("Session {} ({}): {}\n", uuidString, timeString, logMessage);
}
