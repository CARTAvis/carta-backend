/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Frame.h: represents an open image file.  Handles slicing data and region calculations
//# (profiles, histograms, stats)

#ifndef CARTA_BACKEND__FRAME_H_
#define CARTA_BACKEND__FRAME_H_

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/contour.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include <carta-protobuf/tiles.pb.h>

#include "DataStream/Contouring.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "InterfaceConstants.h"
#include "Moment/MomentGenerator.h"
#include "Region/Region.h"
#include "RequirementsCache.h"

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
    Frame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, bool verbose, bool perflog,
        int default_channel = DEFAULT_CHANNEL);
    ~Frame(){};

    bool IsValid();
    std::string GetErrorMessage();

    // Returns pointer to CoordinateSystem clone; caller must delete
    casacore::CoordinateSystem* CoordinateSystem();

    // Image/Frame info
    casacore::IPosition ImageShape();
    size_t NumChannels(); // if no channel axis, nchan=1
    size_t NumStokes();   // if no stokes axis, nstokes=1
    int CurrentChannel();
    int CurrentStokes();
    int StokesAxis();

    // Slicer to set channel and stokes ranges with full xy plane
    casacore::Slicer GetImageSlicer(const ChannelRange& chan_range, int stokes);

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
    bool SetStatsRequirements(int region_id, const std::vector<CARTA::StatsType>& stats_types);
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
    // Check flag if Frame is to be destroyed
    bool IsConnected();

    // Apply Region/Slicer to image (Frame manages image mutex) and get shape, data, or stats
    casacore::LCRegion* GetImageRegion(int file_id, std::shared_ptr<carta::Region> region);
    bool GetImageRegion(int file_id, const ChannelRange& chan_range, int stokes, casacore::ImageRegion& image_region);
    casacore::IPosition GetRegionShape(const casacore::LattRegionHolder& region);
    // Returns data vector
    bool GetRegionData(const casacore::LattRegionHolder& region, std::vector<float>& data);
    bool GetSlicerData(const casacore::Slicer& slicer, std::vector<float>& data);
    // Returns stats_values map for spectral profiles and stats data
    bool GetRegionStats(const casacore::LattRegionHolder& region, std::vector<CARTA::StatsType>& required_stats, bool per_channel,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    bool GetSlicerStats(const casacore::Slicer& slicer, std::vector<CARTA::StatsType>& required_stats, bool per_channel,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    // Spectral profiles from loader
    bool UseLoaderSpectralData(const casacore::IPosition& region_shape);
    bool GetLoaderPointSpectralData(std::vector<float>& profile, int stokes, CARTA::Point& point);
    bool GetLoaderSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress);

    // Moments calculation
    bool CalculateMoments(int file_id, MomentProgressCallback progress_callback, const casacore::ImageRegion& image_region,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
        std::vector<carta::CollapseResult>& collapse_results);
    void StopMomentCalc();
    void IncreaseMomentsCount();
    void DecreaseMomentsCount();

    // Save as a new file or convert it between CASA/FITS formats
    void SaveFile(const std::string& root_folder, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack);

private:
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

    // Histograms: channel is single channel number or ALL_CHANNELS for cube
    int AutoBinSize();
    bool FillHistogramFromCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromLoaderCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromFrameCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool GetCachedImageHistogram(int channel, int stokes, int num_bins, carta::HistogramResults& histogram_results);
    bool GetCachedCubeHistogram(int stokes, int num_bins, carta::HistogramResults& histogram_results);

    // Check for cancel
    bool HasSpectralConfig(const SpectralConfig& config);

    // For convenience, create int map key for storing cache by channel and stokes
    inline int CacheKey(int channel, int stokes) {
        return (channel * 10) + stokes;
    }

    // Get the full name of image file
    std::string GetFileName() {
        return _loader->GetFileName();
    }
    // Get image interface ptr
    casacore::ImageInterface<float>* GetImage() {
        return _loader->GetImage();
    }

    // Setup
    uint32_t _session_id;
    bool _verbose;
    bool _perflog;

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
    std::atomic<int> _z_profile_count;

    // Moments counter, so Frame is not destroyed until finished
    std::atomic<int> _moments_count;

    // Requirements
    std::vector<HistogramConfig> _image_histogram_configs;
    std::vector<HistogramConfig> _cube_histogram_configs;
    std::vector<CARTA::StatsType> _image_required_stats;
    std::vector<std::string> _cursor_spatial_configs;
    std::vector<SpectralConfig> _cursor_spectral_configs;
    std::mutex _spectral_mutex;

    // Cache maps
    // For image, key is cache key (channel/stokes); for cube, key is stokes.
    std::unordered_map<int, std::vector<carta::HistogramResults>> _image_histograms, _cube_histograms;
    std::unordered_map<int, carta::BasicStats<float>> _image_basic_stats, _cube_basic_stats;
    std::unordered_map<int, std::map<CARTA::StatsType, double>> _image_stats;

    // Moment generator
    std::unique_ptr<MomentGenerator> _moment_generator;
};

#endif // CARTA_BACKEND__FRAME_H_
