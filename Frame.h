//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include "ImageData/FileLoader.h"
#include "Region/Region.h"

#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0

#define CURRENT_CHANNEL -1
#define ALL_CHANNELS -2

#define AUTO_BIN_SIZE -1

struct ChannelStats {
    float minVal;
    float maxVal;
    float mean;
    std::vector<float> percentiles;
    std::vector<float> percentileRanks;
    std::vector<int> histogramBins;
    int64_t nanCount;
};

struct CompressionSettings {
    CARTA::CompressionType type;
    float quality;
    int nsubsets;
};

class Frame {

private:
    // setup
    std::string uuid;
    bool valid;
    std::mutex latticeMutex;  // only one disk access at a time

    // image loader, shape, stats from image file
    std::string filename;
    std::unique_ptr<carta::FileLoader> loader;
    casacore::IPosition imageShape; // (width, height, depth, stokes)
    int spectralAxis, stokesAxis;  // axis index for each in 4D image
    size_t nchan;
    std::vector<std::vector<ChannelStats>> channelStats;

    // set image view 
    CARTA::ImageBounds bounds;
    int mip;
    CompressionSettings compression;

    // set image channel
    size_t channelIndex;
    size_t stokesIndex;

    // saved matrix for current channelIndex, stokesIndex
    std::vector<float> channelCache;
    tbb::queuing_rw_mutex cacheMutex; // allow concurrent reads but lock for write
    void setChannelCache(size_t channel, size_t stokes);

    // Region
    // <region_id, Region>: one Region per ID
    std::unordered_map<int, std::unique_ptr<carta::Region>> regions;

    bool loadImageChannelStats(bool loadPercentiles = false);
    void setImageRegion(int regionId); // set region for entire plane image or cube
    void setDefaultCursor(); // using center point of image
    bool cursorSet; // by frontend, not internally

    // Data access for 1 axis (vector profile) or 2 (matrix)
    // fill given matrix for given channel and stokes
    void getChannelMatrix(casacore::Matrix<float>& chanMatrix, size_t channel, size_t stokes);
    // get slicer for matrix with given channel and stokes
    casacore::Slicer getChannelMatrixSlicer(size_t channel, size_t stokes);
    // get lattice slicer for axis profile: full axis if set to -1
    void getLatticeSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes);


public:
    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    ~Frame();

    bool isValid();
    int getMaxRegionId();

    // image data
    // matrix for current channel and stokes
    std::vector<float> getImageData(CARTA::ImageBounds& bounds, int mip, bool meanFilter = true);
    // cube for current stokes
    std::vector<float> getImageChanData(size_t chan);

    // image view
    bool setBounds(CARTA::ImageBounds imageBounds, int newMip);
    void setCompression(CompressionSettings& settings);
    CARTA::ImageBounds currentBounds();
    int currentMip();
    CompressionSettings compressionSettings();

    // image channels
    bool setImageChannels(int newChannel, int newStokes, std::string& message);
    size_t nchannels();
    int currentStokes();
    int currentChannel();

    // region data: pass through to Region
    // SET_REGION fields:
    bool setRegion(int regionId, std::string name, CARTA::RegionType type, int minchan,
        int maxchan, std::vector<int>& stokes, std::vector<CARTA::Point>& points,
        float rotation, std::string& message);
    // setRegion for cursor (defaults for fields not in SET_CURSOR)
    bool setCursorRegion(int regionId, const CARTA::Point& point);
    inline bool isCursorSet() { return cursorSet; }
    void removeRegion(int regionId);

    // set requirements
    bool setRegionHistogramRequirements(int regionId,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles);
    bool setRegionSpectralRequirements(int regionId,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);
    bool setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes);

    // get region histograms
    bool fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData);
    bool getChannelHistogramData(CARTA::RegionHistogramData& histogramData, int chan, int stokes,
        int numbins);  // get existing channel histogram
    bool fillChannelHistogramData(CARTA::RegionHistogramData& histogramData, std::vector<float>& data,
        size_t channel, int numBins, float minval, float maxval);  // make new channel histogram
    int calcAutoNumBins(); // calculate automatic bin size for histogram
    void getMinMax(float& minval, float& maxval, std::vector<float>& data);

    // get profiles, stats
    bool fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData);
    bool fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData);
    bool fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData);
};
