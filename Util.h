#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <chrono>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <cassert>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/File.h>

#include <carta-protobuf/spectral_profile.pb.h>

#include "InterfaceConstants.h"

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

struct ChannelRange {
    int from, to;
    ChannelRange() {
        from = 0;
        to = ALL_CHANNELS;
    }
    ChannelRange(int from_and_to_) {
        from = to = from_and_to_;
    }
    ChannelRange(int from_, int to_) {
        from = from_;
        to = to_;
    }
};

struct RegionState {
    std::string name;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;
    RegionState() {
        name = "";
        type = CARTA::RegionType::RECTANGLE;
        control_points = std::vector<CARTA::Point>();
        rotation = 0;
    }
    RegionState(std::string name_, CARTA::RegionType type_, std::vector<CARTA::Point> control_points_, float rotation_) {
        name = name_;
        type = type_;
        control_points = control_points_;
        rotation = rotation_;
    }
    void operator=(const RegionState& other) {
        name = other.name;
        type = other.type;
        control_points = other.control_points;
        rotation = other.rotation;
    }
    bool operator!=(const RegionState& rhs) {
        if (name != rhs.name || type != rhs.type || rotation != rhs.rotation || control_points.size() != rhs.control_points.size()) {
            return true;
        }
        for (int i = 0; i < control_points.size(); ++i) {
            float x(control_points[i].x()), y(control_points[i].y());
            float rhs_x(rhs.control_points[i].x()), rhs_y(rhs.control_points[i].y());
            if (x != rhs_x || y != rhs_y) {
                return true;
            }
        }
        return false;
    }
    void UpdateState(std::string name_, CARTA::RegionType type_, std::vector<CARTA::Point> control_points_, float rotation_) {
        name = name_;
        type = type_;
        control_points = control_points_;
        rotation = rotation_;
    }
};

#endif // CARTA_BACKEND__UTIL_H_