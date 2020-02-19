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
#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region_histogram.pb.h>

#include "DataStream/Contouring.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "InterfaceConstants.h"
#include "Requirements.h"

struct ViewSettings {
    CARTA::ImageBounds image_bounds;
    int mip;
    CARTA::CompressionType compression_type;
    float quality;
    int num_subsets;
};

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

    // frame info
    casacore::CoordinateSystem CoordinateSystem();
    size_t NumChannels(); // if no channel axis, nchan=1
    size_t NumStokes();   // if no stokes axis, nstokes=1
    int CurrentChannel();
    int CurrentStokes();

    // image view, channels
    bool SetImageView(
        const CARTA::ImageBounds& image_bounds, int new_mip, CARTA::CompressionType compression, float quality, int num_subsets);
    bool SetImageChannels(int new_channel, int new_stokes, std::string& message);

    // raster data
    bool FillRasterImageData(CARTA::RasterImageData& raster_image_data, std::string& message);
    bool FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int channel, int stokes,
        CARTA::CompressionType compression_type, float compression_quality);

    // Functions used for smoothing and contouring
    bool SetContourParameters(const CARTA::SetContourParameters& message);
    inline ContourSettings& GetContourParameters() {
        return _contour_settings;
    };
    bool ContourImage(ContourCallback& partial_contour_callback);

    // histogram data
    bool SetHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms);
    bool FillRegionHistogramData(int region_id, CARTA::RegionHistogramData* histogram_data);
    bool FillHistogram(int channel, int stokes, int num_bins, carta::BasicStats<float>& stats, CARTA::Histogram* histogram);
    bool GetBasicStats(int channel, int stokes, carta::BasicStats<float>& stats);
    bool CalculateHistogram(
        int region_id, int channel, int stokes, int num_bins, carta::BasicStats<float>& stats, carta::HistogramResults& results);
    bool GetCubeHistogramConfig(HistogramConfig& config);
    void CacheCubeStats(int stokes, carta::BasicStats<float>& stats);
    void CacheCubeHistogram(int stokes, carta::HistogramResults& results);

    // stats data
    bool SetStatsRequirements(int region_id, const std::vector<int>& stats_types);
    bool FillRegionStatsData(int region_id, CARTA::RegionStatsData& stats_data);

    // set the flag connected = false, in order to stop the jobs and wait for jobs finished
    void DisconnectCalled();

private:
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

    // cache image plane data for current channel, stokes
    bool FillImageCache();

    // Downsampled data from image cache
    bool GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter = true);
    bool GetRasterTileData(std::vector<float>& tile_data, const Tile& tile, int& width, int& height);

    // fill vector for given channel and stokes
    void GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes);
    // get slicer for xy matrix with given channel and stokes
    casacore::Slicer GetChannelMatrixSlicer(size_t channel, size_t stokes);
    // get image slicer: get full axis (x, y, chan, or stokes) if set to -1, else single index for that axis
    void GetImageSlicer(casacore::Slicer& image_slicer, int x, int y, int channel, int stokes);

    // Histograms: channel is single channel number or ALL_CHANNELS for cube
    int AutoBinSize();
    bool FillHistogramFromCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromLoaderCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool FillHistogramFromFrameCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram);
    bool GetCachedImageHistogram(int channel, int stokes, int num_bins, carta::HistogramResults& histogram_results);
    bool GetCachedCubeHistogram(int stokes, int num_bins, carta::HistogramResults& histogram_results);
    void FillHistogramFromResults(carta::BasicStats<float>& stats, carta::HistogramResults& results, CARTA::Histogram* histogram);

    // setup
    uint32_t _session_id;
    bool _verbose;

    // image opened
    bool _valid;
    std::string _open_image_error;

    // Trigger job cancellation when false
    volatile bool _connected = true;

    // image loader for image type
    std::unique_ptr<carta::FileLoader> _loader;

    // shape, channel, and stokes
    casacore::IPosition _image_shape;  // (width, height, depth, stokes)
    int _spectral_axis, _stokes_axis;  // axis index for each in 4D image
    int _channel_index, _stokes_index; // current channel, stokes for image
    size_t _num_channels;
    size_t _num_stokes;

    // Image settings
    ViewSettings _view_settings;

    // Contour settings
    ContourSettings _contour_settings;

    // Image data handling
    std::vector<float> _image_cache;    // image data for current channelIndex, stokesIndex
    tbb::queuing_rw_mutex _cache_mutex; // allow concurrent reads but lock for write
    std::mutex _image_mutex;            // only one disk access at a time
    bool _cache_loaded;                 // channel cache is set

    // Histogram requirements and caches
    std::vector<HistogramConfig> _image_histogram_configs;
    std::vector<HistogramConfig> _cube_histogram_configs;
    // For image, key is ChannelStokesIndex; for cube, key is stokes. Results are for each config.
    std::unordered_map<int, std::vector<carta::HistogramResults>> _image_histograms, _cube_histograms;
    std::unordered_map<int, carta::BasicStats<float>> _image_basic_stats, _cube_basic_stats;

    // Stats requirements and cache
    std::vector<int> _required_stats;                                          // index is CARTA::StatsType
    std::unordered_map<int, std::map<CARTA::StatsType, double>> _stats_values; // map stats to ChannelStokesIndex
};

#endif // CARTA_BACKEND__FRAME_H_
