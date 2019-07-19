//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#ifndef CARTA_BACKEND__FRAME_H_
#define CARTA_BACKEND__FRAME_H_

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <casacore/images/Images/SubImage.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <tbb/atomic.h>
#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "ImageData/FileLoader.h"
#include "InterfaceConstants.h"
#include "Region/Region.h"
#include "Tile.h"

struct ViewSettings {
    CARTA::ImageBounds image_bounds;
    int mip;
    CARTA::CompressionType compression_type;
    float quality;
    int num_subsets;
};

class Frame {
public:
    Frame(uint32_t session_id, const std::string& filename, const std::string& hdu, const CARTA::FileInfoExtended* info,
        int default_channel = DEFAULT_CHANNEL);
    ~Frame();

    // frame info
    bool IsValid();
    std::vector<int> GetRegionIds();
    int GetMaxRegionId();
    size_t NumChannels(); // if no channel axis, nchan=1
    size_t NumStokes();
    int CurrentChannel();
    int CurrentStokes();

    // Create and remove regions
    bool SetRegion(int region_id, const std::string& name, CARTA::RegionType type, std::vector<CARTA::Point>& points, float rotation,
        std::string& message);
    bool SetCursorRegion(int region_id, const CARTA::Point& point);
    inline bool IsCursorSet() { // set by frontend, not default
        return _cursor_set;
    }
    bool RegionChanged(int region_id);
    void RemoveRegion(int region_id);

    // image view, channels
    bool SetImageView(
        const CARTA::ImageBounds& image_bounds, int new_mip, CARTA::CompressionType compression, float quality, int num_subsets);
    bool SetImageChannels(int new_channel, int new_stokes, std::string& message);

    // set requirements
    bool SetRegionHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool SetRegionSpatialRequirements(int region_id, const std::vector<std::string>& profiles);
    bool SetRegionSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);
    bool SetRegionStatsRequirements(int region_id, const std::vector<int>& stats_types);

    // fill data, profiles, stats messages
    // For some messages, prevent sending data when current channel/stokes changes
    bool FillRasterImageData(CARTA::RasterImageData& raster_image_data, std::string& message);
    bool FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int channel, int stokes,
        CARTA::CompressionType compression_type, float compression_quality);
    bool FillSpatialProfileData(int region_id, CARTA::SpatialProfileData& profile_data, bool stokes_changed = false);
    bool FillSpectralProfileData(std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id,
        bool channel_changed = false, bool stokes_changed = false);
    bool FillRegionHistogramData(int region_id, CARTA::RegionHistogramData* histogram_data, bool channel_changed = false);
    bool FillRegionStatsData(int region_id, CARTA::RegionStatsData& stats_data);

    // histogram only (not full data message) : get if stored, else can calculate
    bool GetRegionMinMax(int region_id, int channel, int stokes, float& min_val, float& max_val);
    bool CalcRegionMinMax(int region_id, int channel, int stokes, float& min_val, float& max_val);
    bool GetImageHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    bool GetRegionHistogram(int region_id, int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    bool CalcRegionHistogram(
        int region_id, int channel, int stokes, int num_bins, float min_val, float max_val, CARTA::Histogram& histogram);
    void SetRegionMinMax(int region_id, int channel, int stokes, float min_val, float max_val);
    void SetRegionHistogram(int region_id, int channel, int stokes, CARTA::Histogram& histogram);

    // set the flag connected = false, in order to stop the jobs and wait for jobs finished
    void DisconnectCalled();

    void IncreaseZProfileCount() {
        ++_z_profile_count;
    }
    void DecreaseZProfileCount() {
        --_z_profile_count;
    }

    // Get current region states
    RegionState GetRegionState(int region_id);

    // Interrupt conditions
    bool Interrupt(const CursorXy& other_cursor_xy);
    bool Interrupt(int region_id, const RegionState& region_state);
    bool Interrupt(int region_id, int profile_index, const RegionState& region_state, const std::vector<int>& requested_stats);

private:
    // Internal regions: image, cursor
    void SetImageRegion(int region_id); // set region for entire plane image or cube
    void SetDefaultCursor();            // using center point of image

    // Image view settings
    void SetViewSettings(
        const CARTA::ImageBounds& new_bounds, int new_mip, CARTA::CompressionType new_compression, float new_quality, int new_subsets);
    inline ViewSettings GetViewSettings() {
        return _view_settings;
    };

    // validate channel, stokes index values
    bool CheckChannel(int channel);
    bool CheckStokes(int stokes);

    // Check whether channels have changed
    bool ChannelsChanged(int channel, int stokes);

    // Image data
    // save image region data for current channel, stokes
    void SetImageCache();
    // downsampled data from image cache
    bool GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter = true);
    bool GetRasterTileData(std::vector<float>& tile_data, const Tile& tile, int& width, int& height);
    // fill vector for given channel and stokes
    void GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes);
    // get slicer for xy matrix with given channel and stokes
    casacore::Slicer GetChannelMatrixSlicer(size_t channel, size_t stokes);
    // get lattice slicer for profiles: get full axis if set to -1, else single value for that axis
    void GetImageSlicer(casacore::Slicer& image_slicer, int x, int y, int channel, int stokes);
    // make Lattice sublattice from Region given channel and stokes
    bool GetRegionSubImage(int region_id, casacore::SubImage<float>& sub_image, int stokes, ChannelRange channel_range);

    // histogram helper
    int CalcAutoNumBins(int region_id); // calculate automatic bin size for region

    // get cursor's x-y coordinate from subimage
    bool GetSubImageXy(casacore::SubImage<float>& sub_image, CursorXy& cursor_xy);
    // get spectral profile data from subimage
    bool GetCursorSpectralData(
        std::vector<float>& data, casacore::SubImage<float>& sub_image, std::function<void(std::vector<float>, float)> cb);
    // get regional spectral profile (statistics) data
    bool GetRegionSpectralData(std::vector<std::vector<double>>& stats_values, int region_id, int profile_index, int profile_stokes,
        const std::function<void(std::vector<std::vector<double>>, float)>& partial_results_callback);

    // Functions used to set cursor and region states
    void SetConnectionFlag(bool connected);
    void SetCursorXy(int x, int y);
    void SetRegionState(int region_id, std::string name, CARTA::RegionType type, std::vector<CARTA::Point> points, float rotation);
    void SetRegionSpectralRequests(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles);

    // Functions used to check cursor and region states
    bool IsConnected();
    bool IsSameCursorXy(const CursorXy& other_cursor_xy);
    bool IsSameRegionState(int region_id, const RegionState& region_state);
    bool AreSameRegionSpectralRequests(int region_id, int profile_index, const std::vector<int>& requested_stats);

    // setup
    uint32_t _session_id;
    bool _valid;

    // spectral profile counter, which is used to determine whether the Frame object can be destroyed (_z_profile_count == 0 ?).
    tbb::atomic<int> _z_profile_count;

    // image loader, stats from image file
    std::string _filename;
    std::unique_ptr<carta::FileLoader> _loader;

    // shape, channel, and stokes
    casacore::IPosition _image_shape;  // (width, height, depth, stokes)
    int _spectral_axis, _stokes_axis;  // axis index for each in 4D image
    int _channel_index, _stokes_index; // current channel, stokes for image
    size_t _num_channels;
    size_t _num_stokes;

    // Image settings
    ViewSettings _view_settings;

    // Image data handling
    std::vector<float> _image_cache;    // image data for current channelIndex, stokesIndex
    tbb::queuing_rw_mutex _cache_mutex; // allow concurrent reads but lock for write
    std::mutex _image_mutex;            // only one disk access at a time
    bool _cache_loaded;                 // channel cache is set

    // Region
    std::unordered_map<int, std::unique_ptr<carta::Region>> _regions; // key is region ID
    bool _cursor_set;                                                 // cursor region set by frontend, not internally

    // Communication
    volatile bool _connected = true;
    // Current cursor's x-y coordinate
    CursorXy _cursor_xy;
    // Current region states
    std::unordered_map<int, RegionState> _region_states;
    // Current region configs
    std::unordered_map<int, RegionRequest> _region_requests;
};

#endif // CARTA_BACKEND__FRAME_H_
