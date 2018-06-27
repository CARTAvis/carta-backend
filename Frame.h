#pragma once
#include <H5Cpp.h>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <carta-protobuf/defs.pb.h>

struct ChannelStats {
    float minVal;
    float maxVal;
    float mean;
    std::vector<float> percentiles;
    std::vector<float> percentileRanks;
    std::vector<int> histogramBins;
    int64_t nanCount;
};

struct ImageInfo {
    std::string filename;
    std::string unit;
    int depth;
    int width;
    int height;
    int stokes;
    int dimensions;
    std::vector<std::vector<ChannelStats>> channelStats;
};

class Frame {
public:
    std::vector<float> channelCache;
    std::vector<float> zProfileCache;
    std::vector<int> zProfileCoords;
    int currentChannel;
    int currentStokes;

    H5::H5File file;
    H5::Group hduGroup;
    std::map<std::string, H5::DataSet> dataSets;
    ImageInfo imageInfo;
    bool valid;

    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    bool setChannels(int channel, int stokes);
    bool loadStats();
    std::vector<float> getImageData(CARTA::ImageBounds imageBounds, int mip, bool meanFilter = true);
private:
    std::string uuid;
};