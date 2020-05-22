#ifndef CARTA_BACKEND_TIMER_TIMER_H_
#define CARTA_BACKEND_TIMER_TIMER_H_

#include <chrono>
#include <unordered_map>

#include <fmt/format.h>

typedef std::chrono::time_point<std::chrono::high_resolution_clock> timer_entry;
typedef std::chrono::duration<double, std::milli> timer_duration;
class Timer {
public:
    void Start(const std::string& timer_name);
    void End(const std::string& timer_name);
    void Clear(const std::string& timer_name = "");
    timer_duration GetMeasurement(const std::string& timer_name) const;
    std::string GetMeasurementString(const std::string& timer_name) const;
    void Print(const std::string& timer_name = "") const;
protected:
    std::unordered_map<std::string, timer_entry> _entries;
    std::unordered_map<std::string, std::pair<timer_duration, int>> _measurements;
};

#endif //CARTA_BACKEND_TIMER_TIMER_H_
