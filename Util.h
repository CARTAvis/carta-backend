#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <cassert>
#include <chrono>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/File.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/mirlib/miriad.h>

#include <carta-protobuf/region_requirements.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "InterfaceConstants.h"

#ifdef __linux__
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#elif __APPLE__
#include <filesystem>
namespace fs = std::__fs::filesystem;
#endif

void LOG(uint32_t id, const std::string& log_message);

template <typename... Args>
inline void LOG(uint32_t id, const char* template_string, Args... args) {
    LOG(id, fmt::format(template_string, args...));
}

template <typename... Args>
inline void LOG(uint32_t id, const std::string& template_string, Args... args) {
    LOG(id, fmt::format(template_string, args...));
}

void ReadPermissions(const std::string& filename, std::unordered_map<std::string, std::vector<std::string>>& permissions_map);
bool CheckRootBaseFolders(std::string& root, std::string& base);
uint32_t GetMagicNumber(const std::string& filename);

// split input string into a vector of strings by delimiter
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);

CARTA::FileType GetCartaFileType(const std::string& filename);

// ************ structs *************
//
// Usage of the ChannelRange:
//
// ChannelRange() defines all channels
// ChannelRange(0) defines a single channel range, channel 0, in this example
// ChannelRange(0, 1) defines the channel range between 0 and 1 (including), in this example
// ChannelRange(0, 2) defines the channel range between 0 and 2, i.e., [0, 1, 2] in this example
//
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

struct CursorXy {
    // CARTA::Point is float
    float x, y;
    CursorXy() {
        x = -1.0;
        y = -1.0;
    }
    CursorXy(float x_, float y_) {
        x = x_;
        y = y_;
    }
    void operator=(const CursorXy& other) {
        x = other.x;
        y = other.y;
    }
    bool operator==(const CursorXy& rhs) const {
        if ((x != rhs.x) || (y != rhs.y)) {
            return false;
        }
        return true;
    }
};

struct RegionState {
    std::string name;
    CARTA::RegionType type;
    std::vector<CARTA::Point> control_points;
    float rotation;
    RegionState() {}
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
    bool operator==(const RegionState& rhs) {
        if (name != rhs.name || type != rhs.type || rotation != rhs.rotation || control_points.size() != rhs.control_points.size()) {
            return false;
        }
        for (int i = 0; i < control_points.size(); ++i) {
            float x(control_points[i].x()), y(control_points[i].y());
            float rhs_x(rhs.control_points[i].x()), rhs_y(rhs.control_points[i].y());
            if (x != rhs_x || y != rhs_y) {
                return false;
            }
        }
        return true;
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

struct SpectralConfig {
    int stokes_index;
    std::vector<int> stats_types;

    SpectralConfig() {}
    SpectralConfig(int stokes_index_, std::vector<int> stats_types_) {
        stokes_index = stokes_index_;
        stats_types = stats_types_;
    }
    bool operator==(const SpectralConfig& rhs) const {
        return ((stokes_index == rhs.stokes_index) && (stats_types == rhs.stats_types));
    }
};

#endif // CARTA_BACKEND__UTIL_H_
