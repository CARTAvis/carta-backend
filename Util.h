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
#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "InterfaceConstants.h"

// ************ Logging *************

void Log(uint32_t id, const std::string& log_message);

template <typename... Args>
inline void Log(uint32_t id, const char* template_string, Args... args) {
    Log(id, fmt::format(template_string, args...));
}

template <typename... Args>
inline void Log(uint32_t id, const std::string& template_string, Args... args) {
    Log(id, fmt::format(template_string, args...));
}

// ************ Utilities *************

void ReadPermissions(const std::string& filename, std::unordered_map<std::string, std::vector<std::string>>& permissions_map);
bool CheckRootBaseFolders(std::string& root, std::string& base);

// split input string into a vector of strings by delimiter
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

// Image info: filename, type
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);
CARTA::FileType GetCartaFileType(const std::string& filename);

// For convenience, create single index for storing cache by channel and stokes
inline int ChannelStokesIndex(int channel, int stokes) {
    return (channel * 10) + stokes;
}

// ************ Data Stream Helpers *************

void ConvertCoordinateToAxes(const std::string& coordinate, int& axis_index, int& stokes_index);

void FillHistogramFromResults(CARTA::Histogram* histogram, carta::BasicStats<float>& stats, carta::HistogramResults& results);

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, std::string& coordinate, std::vector<int>& required_stats,
    std::map<CARTA::StatsType, std::vector<double>>& spectral_data);

void FillStatisticsValuesFromMap(
    CARTA::RegionStatsData& stats_data, std::vector<int>& required_stats, std::map<CARTA::StatsType, double>& stats_value_map);

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

struct PointXy {
    // Utilities for cursor and point regions
    float x, y;

    PointXy() {
        x = -1.0;
        y = -1.0;
    }
    PointXy(float x_, float y_) {
        x = x_;
        y = y_;
    }
    void operator=(const PointXy& other) {
        x = other.x;
        y = other.y;
    }
    bool operator==(const PointXy& rhs) const {
        if ((x != rhs.x) || (y != rhs.y)) {
            return false;
        }
        return true;
    }
    void ToIndex(int& x_index, int& y_index) {
        // convert float to int for index into image data array
        x_index = static_cast<int>(std::round(x));
        y_index = static_cast<int>(std::round(y));
    }
    bool InImage(int xrange, int yrange) {
        // returns whether x, y are within given image axis ranges
        int x_index, y_index;
        ToIndex(x_index, y_index);
        bool x_in_image = (x_index >= 0) && (x_index < xrange);
        bool y_in_image = (y_index >= 0) && (y_index < yrange);
        return (x_in_image && y_in_image);
    }
};

#endif // CARTA_BACKEND__UTIL_H_
