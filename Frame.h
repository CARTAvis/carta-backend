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

class Frame {
private:
    bool valid;
    int channelIndex;
    int stokesIndex;
    std::string uuid;
    std::string filename;
    std::string unit;
    int width;
    int height;
    int depth;
    int stokes;
    int dimensions;
    std::vector<float> channelCache;
    std::vector<float> zProfileCache;
    std::vector<int> zProfileCoords;
    std::vector<std::vector<ChannelStats>> channelStats;
    H5::H5File file;
    H5::Group hduGroup;
    std::map<std::string, H5::DataSet> dataSets;

public:
    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    bool setChannels(int newChannel, int newStokes);
    bool loadStats();
    bool isValid();
    int currentStokes();
    int currentChannel();
    std::vector<float> getImageData(CARTA::ImageBounds imageBounds, int mip, bool meanFilter = true);
    CARTA::Histogram currentHistogram();
};