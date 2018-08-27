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
    CARTA::ImageBounds bounds;
    int mip;
    std::vector<float> channelCache;
    std::vector<float> zProfileCache;
    std::vector<int> zProfileCoords;
    std::vector<std::vector<ChannelStats>> channelStats;
    H5::H5File file;
    H5::Group hduGroup;
    std::map<std::string, H5::DataSet> dataSets;

public:
    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    bool setBounds(CARTA::ImageBounds imageBounds, int newMip);
    bool setChannels(int newChannel, int newStokes);
    bool loadStats(bool loadPercentiles = false);
    bool isValid();
    int currentStokes();
    int currentChannel();
    CARTA::ImageBounds currentBounds();
    int currentMip();

    std::vector<float> getImageData(bool meanFilter = true);
    std::vector<float> getXProfile(int y, int profileChannel, int profileStokes, int startX = -1, int endX = -1);
    std::vector<float> getYProfile(int x, int profileChannel, int profileStokes, int startY = -1, int endY = -1);
    //std::vector<float> getZProfile(int x, int y, int profileStokes, int startZ = -1, int endZ = -1);
    CARTA::Histogram currentHistogram();
};