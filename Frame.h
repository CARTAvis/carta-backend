//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#ifndef CARTA_BACKEND__FRAME_H_
#define CARTA_BACKEND__FRAME_H_

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <tbb/atomic.h>
#include <tbb/queuing_rw_mutex.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Images/SubImage.h>

#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "ImageData/FileLoader.h"
#include "InterfaceConstants.h"
#include "Region/Region.h"

struct ViewSettings {
    CARTA::ImageBounds image_bounds;
    int mip;
    CARTA::CompressionType compression_type;
    float quality;
    int num_subsets;
};

class Frame {
private:
    // setup
    uint32_t sessionId;
    bool valid;

    // communication
    bool connected;

    // spectral profile counter
    tbb::atomic<int> zProfileCount;

    // image loader, stats from image file
    std::string filename;
    std::unique_ptr<carta::FileLoader> loader;

    // shape, channel, and stokes
    casacore::IPosition imageShape; // (width, height, depth, stokes)
    int spectralAxis, stokesAxis;   // axis index for each in 4D image
    int channelIndex, stokesIndex;  // current channel, stokes for image
    size_t nchan;
    size_t nstok;

    // Image settings
    ViewSettings view_settings_;

    std::vector<float> imageCache;    // image data for current channelIndex, stokesIndex
    tbb::queuing_rw_mutex cacheMutex; // allow concurrent reads but lock for write
    std::mutex imageMutex;            // only one disk access at a time
    bool cacheLoaded;                 // channel cache is set

    // Region
    std::unordered_map<int, std::unique_ptr<carta::Region>> regions; // key is region ID
    bool cursorSet;                                                  // cursor region set by frontend, not internally

    // Internal regions: image, cursor
    void setImageRegion(int regionId); // set region for entire plane image or cube
    void setDefaultCursor();           // using center point of image

    // Image view settings
    void setViewSettings(
        const CARTA::ImageBounds& newBounds, int newMip, CARTA::CompressionType newCompression, float newQuality, int newSubsets);
    inline ViewSettings getViewSettings() {
        return view_settings_;
    };

    // validate channel, stokes index values
    bool checkChannel(int channel);
    bool checkStokes(int stokes);

    // Image data and slicers
    // save Image region data for current channel, stokes
    void setImageCache();
    // downsampled data from image cache
    bool getRasterData(std::vector<float>& imageData, CARTA::ImageBounds& bounds, int mip, bool meanFilter = true);

    // fill vector for given channel and stokes
    void getChannelMatrix(std::vector<float>& chanMatrix, size_t channel, size_t stokes);
    // get slicer for xy matrix with given channel and stokes
    casacore::Slicer getChannelMatrixSlicer(size_t channel, size_t stokes);
    // get lattice slicer for profiles: get full axis if set to -1, else single value for that axis
    void getImageSlicer(casacore::Slicer& imageSlicer, int x, int y, int channel, int stokes);
    // xy region created (subset of image)
    bool xyRegionValid(int regionId);
    // make Lattice sublattice from Region given channel and stokes
    bool getRegionSubImage(int regionId, casacore::SubImage<float>& subimage, int stokes, int channel=ALL_CHANNELS);
    // add pixel mask to sublattice for stats
    //void setPixelMask(casacore::SubImage<float>& subimage);
    //void generatePixelMask(casacore::ArrayLattice<bool>& pixelMask, casacore::SubImage<float>& subimage);

    // histogram helper
    int calcAutoNumBins(int regionId); // calculate automatic bin size for region

    // current cursor's x-y coordinate
    std::pair<int, int> cursorXY;
    // get cursor's x-y coordinate forom sub-lattice
    bool getSubimageXY(casacore::SubImage<float>& subimage, std::pair<int, int>& cursor_xy);
    // get spectral profile data from sub-lattice
    bool getSpectralData(std::vector<float>& data, casacore::SubImage<float>& subimage, int checkPerChannels=ALL_CHANNELS);

    void increaseZProfileCount() {
        ++zProfileCount;
    }
    void decreaseZProfileCount() {
        --zProfileCount;
    }

public:
    Frame(uint32_t sessionId, const std::string& filename, const std::string& hdu, int defaultChannel = DEFAULT_CHANNEL);
    ~Frame();

    // frame info
    bool isValid();
    std::vector<int> getRegionIds();
    int getMaxRegionId();
    size_t nchannels(); // if no channel axis, nchan=1
    size_t nstokes();
    int currentChannel();
    int currentStokes();

    // Create and remove regions
    bool setRegion(
        int regionId, std::string name, CARTA::RegionType type, std::vector<CARTA::Point>& points, float rotation, std::string& message);
    bool setCursorRegion(int regionId, const CARTA::Point& point);
    inline bool isCursorSet() {
        return cursorSet;
    } // set by frontend, not default
    bool regionChanged(int regionId);
    void removeRegion(int regionId);

    // image view, channels
    bool setImageView(const CARTA::ImageBounds& imageBounds, int newMip, CARTA::CompressionType compression, float quality, int nsubsets);
    bool setImageChannels(int newChannel, int newStokes, std::string& message);

    // set requirements
    bool setRegionHistogramRequirements(int regionId, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles);
    bool setRegionSpectralRequirements(int regionId, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);
    bool setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes);

    // fill data, profiles, stats messages
    // For some messages, only fill if requirements are for current channel/stokes
    bool fillRasterImageData(CARTA::RasterImageData& rasterImageData, std::string& message);
    bool fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData, bool checkCurrentStokes = false);
    bool fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData, bool checkCurrentStokes = false);
    bool fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData, bool checkCurrentChan = false);
    bool fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData);

    // histogram only (not full data message) : get if stored, else can calculate
    bool getRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval);
    bool calcRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval);
    bool getImageHistogram(int channel, int stokes, int nbins, CARTA::Histogram& histogram);
    bool getRegionHistogram(int regionId, int channel, int stokes, int nbins, CARTA::Histogram& histogram);
    bool calcRegionHistogram(int regionId, int channel, int stokes, int nbins, float minval, float maxval, CARTA::Histogram& histogram);
    void setRegionMinMax(int regionId, int channel, int stokes, float minval, float maxval);
    void setRegionHistogram(int regionId, int channel, int stokes, CARTA::Histogram& histogram);

    // set the flag connected = false, in order to stop the jobs and wait for jobs finished
    void disconnectCalled();
};

#endif // CARTA_BACKEND__FRAME_H_
