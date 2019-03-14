//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include "InterfaceConstants.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "compression.h"

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

    // image loader, stats from image file
    std::string filename;
    std::unique_ptr<carta::FileLoader> loader;
    std::vector<std::vector<ChannelStats>> channelStats;

    // shape
    casacore::IPosition imageShape; // (width, height, depth, stokes)
    int spectralAxis, stokesAxis;   // axis index for each in 4D image
    int channelIndex, stokesIndex;  // current channel, stokes for image
    size_t nchan;

    // Image settings
    // view and compression
    CARTA::ImageBounds bounds;
    int mip, nsubsets;
    CARTA::CompressionType compType;
    float quality;
    // channel and stokes

    std::vector<float> imageCache;  // image data for current channelIndex, stokesIndex
    tbb::queuing_rw_mutex cacheMutex; // allow concurrent reads but lock for write
    std::mutex latticeMutex;          // only one disk access at a time
    bool cacheLoaded;                 // channel cache is set

    // Region
    std::unordered_map<int, std::unique_ptr<carta::Region>> regions;  // key is region ID
    bool cursorSet; // cursor region set by frontend, not internally

    // Stats stored in image file
    bool loadImageChannelStats(bool loadPercentiles = false);

    // Internal regions: image, cursor
    void setImageRegion(int regionId); // set region for entire plane image or cube
    void setDefaultCursor(); // using center point of image

    // Image data and slicers
    void setChannelCache();
    bool getImageData(std::vector<float>& imageData, bool meanFilter = true); // downsampled
    // fill vector for given channel and stokes
    void getChannelMatrix(std::vector<float>& chanMatrix, size_t channel, size_t stokes);
    // get slicer for xy matrix with given channel and stokes
    casacore::Slicer getChannelMatrixSlicer(size_t channel, size_t stokes);
    // get lattice slicer for profiles: get full axis if set to -1, else single value for that axis
    void getLatticeSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes);

    // histogram helpers
    int calcAutoNumBins(); // calculate automatic bin size


public:
    Frame(const std::string& uuidString, const std::string& filename, const std::string& hdu, int defaultChannel = DEFAULT_CHANNEL);
    ~Frame();

    // frame info
    bool isValid();
    int getMaxRegionId();
    size_t nchannels(); // if no channel axis, nchan=1
    int currentStokes();

    // Create and remove regions
    bool setRegion(int regionId, std::string name, CARTA::RegionType type, int minchan,
        int maxchan, std::vector<CARTA::Point>& points, float rotation, std::string& message);
    bool setCursorRegion(int regionId, const CARTA::Point& point);
    inline bool isCursorSet() { return cursorSet; } // set by frontend, not default
    void removeRegion(int regionId);

    // image view, channels
    bool setImageView(const CARTA::ImageBounds& imageBounds, int newMip,
        CARTA::CompressionType compression, float quality, int nsubsets);
    bool setImageChannels(int newChannel, int newStokes, std::string& message);

    // set requirements
    bool setRegionHistogramRequirements(int regionId,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles);
    bool setRegionSpectralRequirements(int regionId,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);
    bool setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes);

    // fill data, profiles, stats messages
    bool fillRasterImageData(CARTA::RasterImageData& rasterImageData, std::string& message);
    bool fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData);
    bool fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData);
    bool fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData);
    bool fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData);

    // histogram only (not full data message) : get if stored, else can calculate
    bool getRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval);
    bool calcRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval);
    bool getRegionHistogram(int regionId, int channel, int stokes, int nbins,
        CARTA::Histogram& histogram);
    bool calcRegionHistogram(int regionId, int channel, int stokes, int nbins, float minval,
        float maxval, CARTA::Histogram& histogram);
    void setRegionMinMax(int regionId, int channel, int stokes, float minval, float maxval);
    void setRegionHistogram(int regionId, int channel, int stokes, CARTA::Histogram& histogram);
};
