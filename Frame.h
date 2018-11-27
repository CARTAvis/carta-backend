//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <tbb/concurrent_queue.h>

#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include "ImageData/FileLoader.h"
#include "Region/Region.h"

#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0

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
    // setup
    std::string uuid;
    bool valid;
    std::mutex mutex;  // only one disk access at a time

    // image loader, shape, stats from image file
    std::string filename;
    std::unique_ptr<carta::FileLoader> loader;
    casacore::IPosition imageShape; // (width, height, depth, stokes)
    size_t ndims;
    int spectralAxis, stokesAxis;  // axis index for each in 4D image
    std::vector<std::vector<ChannelStats>> channelStats;

    // set image view 
    CARTA::ImageBounds bounds;
    int mip;

    // set image channel
    size_t channelIndex;
    size_t stokesIndex;

    // saved matrix for channelIndex, stokesIndex
    casacore::Matrix<float> channelCache;

    // Region
    // <region_id, Region>: one Region per ID
    std::unordered_map<int, std::unique_ptr<carta::Region>> regions;

    bool loadImageChannelStats(bool loadPercentiles = false);
    void setImageRegion(); // set region for entire image
    // fill given matrix for given channel and stokes
    casacore::Slicer getChannelMatrixSlicer(size_t channel, size_t stokes);
    void getChannelMatrix(casacore::Matrix<float>& chanMatrix, size_t channel, size_t stokes);
    // get image data slicer for axis profile: whichever axis is set to -1
    void getProfileSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes);

public:
    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    ~Frame();

    bool isValid();
    int getMaxRegionId();

    // image data
    std::vector<float> getImageData(bool meanFilter = true);

    // image view
    bool setBounds(CARTA::ImageBounds imageBounds, int newMip);
    CARTA::ImageBounds currentBounds();
    int currentMip();

    // image channels
    bool setImageChannels(int newChannel, int newStokes, std::string& message);
    int currentStokes();
    int currentChannel();

    // region data: pass through to Region
    // SET_REGION fields:
    bool setRegion(int regionId, std::string name, CARTA::RegionType type, int minchan,
        int maxchan, std::vector<int>& stokes, std::vector<CARTA::Point>& points,
        float rotation, std::string& message);
    // setRegion for cursor (defaults for fields not in SET_CURSOR)
    bool setCursorRegion(int regionId, const CARTA::Point& point);
    void removeRegion(int regionId);

    // set requirements
    bool setRegionHistogramRequirements(int regionId,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles);
    bool setRegionSpectralRequirements(int regionId,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);
    bool setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes);

    // get region histograms, profiles, stats
    bool fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData);
    bool fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData);
    bool fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData);
    bool fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData);
};
