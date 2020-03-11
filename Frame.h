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

#include <carta-protobuf/contour.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include <carta-protobuf/tiles.pb.h>

#include "DataStream/Contouring.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "InterfaceConstants.h"
#include "Requirements.h"

struct ContourSettings {
    std::vector<double> levels;
    CARTA::SmoothingMode smoothing_mode;
    int smoothing_factor;
    int decimation;
    int compression_level;
    int chunk_size;
    uint32_t reference_file_id;

    // Equality operator for checking if contour settings have changed
    bool operator==(const ContourSettings& rhs) const {
        if (this->smoothing_mode != rhs.smoothing_mode || this->smoothing_factor != rhs.smoothing_factor ||
            this->decimation != rhs.decimation || this->compression_level != rhs.compression_level ||
            this->reference_file_id != rhs.reference_file_id || this->chunk_size != rhs.chunk_size) {
            return false;
        }
        if (this->levels.size() != rhs.levels.size()) {
            return false;
        }

        for (auto i = 0; i < this->levels.size(); i++) {
            if (this->levels[i] != rhs.levels[i]) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const ContourSettings& rhs) const {
        return !(*this == rhs);
    }
};

class Frame {
public:
    Frame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, bool verbose, int default_channel = DEFAULT_CHANNEL);
    ~Frame(){};

    bool IsValid();
    std::string GetErrorMessage();

    // Frame info
    casacore::CoordinateSystem CoordinateSystem();
    size_t NumChannels(); // if no channel axis, nchan=1
    size_t NumStokes();   // if no stokes axis, nstokes=1
    int CurrentChannel();
    int CurrentStokes();

    // Image view, channels
    inline void SetAnimationViewSettings(const CARTA::AddRequiredTiles& required_animation_tiles) {
        _required_animation_tiles = required_animation_tiles;
    }
    inline CARTA::AddRequiredTiles GetAnimationViewSettings() {
        return _required_animation_tiles;
    };
    bool SetImageChannels(int new_channel, int new_stokes, std::string& message);

    // Cursor
    bool SetCursor(float x, float y);

    // Raster data
    bool FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int channel, int stokes,
        CARTA::CompressionType compression_type, float compression_quality);

    // Functions used for smoothing and contouring
    bool SetContourParameters(const CARTA::SetContourParameters& message);
    inline ContourSettings& GetContourParameters() {
        return _contour_settings;
    };
    bool ContourImage(ContourCallback& partial_contour_callback);

    // Histograms: image and cube
    bool SetHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_configs);
    bool FillRegionHistogramData(int region_id, CARTA::RegionHistogramData& histogram_data);
    bool FillHistogram(int channel, int stokes, int num_bins, carta::BasicStats<float>& stats, CARTA::Histogram* histogram);
    bool GetBasicStats(int channel, int stokes, carta::BasicStats<float>& stats);
    bool CalculateHistogram(
        int region_id, int channel, int stokes, int num_bins, carta::BasicStats<float>& stats, carta::HistogramResults& results);
    bool GetCubeHistogramConfig(HistogramConfig& config);
    void CacheCubeStats(int stokes, carta::BasicStats<float>& stats);
    void CacheCubeHistogram(int stokes, carta::HistogramResults& results);

    // Stats: image
    bool SetStatsRequirements(int region_id, const std::vector<int>& stats_types);
    bool FillRegionStatsData(int region_id, CARTA::RegionStatsData& stats_data);

    // Spatial: cursor
    bool SetSpatialRequirements(int region_id, const std::vector<std::string>& spatial_profiles);
    bool FillSpatialProfileData(int region_id, CARTA::SpatialProfileData& spatial_data);

    // Spectral: cursor
    bool SetSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_configs);
    bool FillSpectralProfileData(std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, bool stokes_changed);
    void IncreaseZProfileCount();
    void DecreaseZProfileCount();

    // Set the flag connected = false, in order to stop the jobs and wait for jobs finished
    void DisconnectCalled();

    // Apply Region/Slicer to image (Frame manages image mutex)
	casacore::IPosition GetRegionShape(const casacore::LattRegionHolder& region);
    // Returns data vector
    bool GetRegionData(const casacore::LattRegionHolder& region, std::vector<float>& data);
    bool GetSlicerData(const casacore::Slicer& slicer, std::vector<float>& data);
    // Returns stats_values map
    bool GetRegionStats(const casacore::LattRegionHolder& region, std::vector<int>& required_stats, bool per_channel,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    bool GetSlicerStats(const casacore::Slicer& slicer, std::vector<int>& required_stats, bool per_channel,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);

private:
    // Check flag if Frame is to be destroyed
    bool IsConnected();

    // Validate channel, stokes index values
    bool CheckChannel(int channel);
    bool CheckStokes(int stokes);

    // Check whether channels have changed
    bool ChannelsChanged(int channel, int stokes);

    // Cache image plane data for current channel, stokes
    bool FillImageCache();

    // Downsampled data from image cache
    bool GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter = true);
    bool GetRasterTileData(std::vector<float>& tile_data, const Tile& tile, int& width, int& height);

    // Fill vector for given channel and stokes
    void GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes);
    // Get slicer for xy matrix with given channel and stokes
    casacore::Slicer GetChannelMatrixSlicer(size_t channel, size_t stokes);

    // Histograms: channel is single channel number or ALL_CHANNELS for cube
    int AutoBinSize();
    bool FillHistogramFromCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromLoaderCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromFrameCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool GetCachedImageHistogram(int channel, int stokes, int num_bins, carta::HistogramResults& histogram_results);
    bool GetCachedCubeHistogram(int stokes, int num_bins, carta::HistogramResults& histogram_results);

    // Setup
    uint32_t _session_id;
    bool _verbose;

    // Image opened
    bool _valid;
    std::string _open_image_error;

    // Trigger job cancellation when false
    volatile bool _connected = true;

    // Image loader for image type
    std::unique_ptr<carta::FileLoader> _loader;

    // Shape, channel, and stokes
    casacore::IPosition _image_shape;  // (width, height, depth, stokes)
    int _spectral_axis, _stokes_axis;  // axis index for each in 4D image
    int _channel_index, _stokes_index; // current channel, stokes for image
    size_t _num_channels;
    size_t _num_stokes;

    // Image settings
    CARTA::AddRequiredTiles _required_animation_tiles;

    // Current cursor position
    PointXy _cursor;

    // Contour settings
    ContourSettings _contour_settings;

    // Image data cache and mutex
    std::vector<float> _image_cache;    // image data for current channelIndex, stokesIndex
    tbb::queuing_rw_mutex _cache_mutex; // allow concurrent reads but lock for write
    std::mutex _image_mutex;            // only one disk access at a time

    // Spectral profile counter, so Frame is not destroyed until finished
    tbb::atomic<int> _z_profile_count;

    // Requirements
    std::vector<HistogramConfig> _image_histogram_configs;
    std::vector<HistogramConfig> _cube_histogram_configs;
    std::vector<int> _image_required_stats;
    std::vector<std::string> _cursor_spatial_configs;
    std::vector<SpectralConfig> _cursor_spectral_configs;

    // Cache maps
    // For image, key is ChannelStokesIndex; for cube, key is stokes.
    std::unordered_map<int, std::vector<carta::HistogramResults>> _image_histograms, _cube_histograms;
    std::unordered_map<int, carta::BasicStats<float>> _image_basic_stats, _cube_basic_stats;
    std::unordered_map<int, std::map<CARTA::StatsType, double>> _image_stats;
};

#endif // CARTA_BACKEND__FRAME_H_
