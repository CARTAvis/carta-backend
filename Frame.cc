#include "Frame.h"

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <fstream>
#include <thread>

#include <casacore/tables/DataMan/TiledFileAccess.h>

#include "DataStream/Compression.h"
#include "DataStream/Contouring.h"
#include "DataStream/Smoothing.h"
#include "ImageStats/StatsCalculator.h"
#include "Util.h"

using namespace carta;

Frame::Frame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, bool verbose, int default_channel)
    : _session_id(session_id),
      _verbose(verbose),
      _valid(true),
      _loader(loader),
      _spectral_axis(-1),
      _stokes_axis(-1),
      _channel_index(-1),
      _stokes_index(-1),
      _num_channels(1),
      _num_stokes(1),
      _z_profile_count(0) {
    if (!_loader) {
        _open_image_error = fmt::format("Problem loading image: image type not supported.");
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    try {
        _loader->OpenFile(hdu);
    } catch (casacore::AipsError& err) {
        _open_image_error = fmt::format("Problem opening image: {}", err.getMesg());
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    // Get shape and axis values from the loader
    std::string log_message;
    if (!_loader->FindCoordinateAxes(_image_shape, _spectral_axis, _stokes_axis, log_message)) {
        _open_image_error = fmt::format("Problem determining file shape: {}", log_message);
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }
    _num_channels = (_spectral_axis >= 0 ? _image_shape(_spectral_axis) : 1);
    _num_stokes = (_stokes_axis >= 0 ? _image_shape(_stokes_axis) : 1);

    // set current channel, stokes, imageCache
    _channel_index = default_channel;
    _stokes_index = DEFAULT_STOKES;
    if (!FillImageCache()) {
        _valid = false;
        return;
    }

    // set default histogram requirements
    _image_histogram_configs.clear();
    _cube_histogram_configs.clear();
    HistogramConfig config;
    config.channel = CURRENT_CHANNEL;
    config.num_bins = AUTO_BIN_SIZE;
    _image_histogram_configs.push_back(config);

    try {
        // Resize stats vectors and load data from image, if the format supports it.
        // A failure here shouldn't invalidate the frame
        _loader->LoadImageStats();
    } catch (casacore::AipsError& err) {
        _open_image_error = fmt::format("Problem loading statistics from file: {}", err.getMesg());
        if (_verbose) {
            Log(session_id, _open_image_error);
        }
    }
}

bool Frame::IsValid() {
    return _valid;
}

std::string Frame::GetErrorMessage() {
    return _open_image_error;
}

casacore::CoordinateSystem Frame::CoordinateSystem() {
    casacore::CoordinateSystem csys;
    if (IsValid()) {
        _loader->GetCoordinateSystem(csys);
    }
    return csys;
}

size_t Frame::NumChannels() {
    return _num_channels;
}

size_t Frame::NumStokes() {
    return _num_stokes;
}

int Frame::CurrentChannel() {
    return _channel_index;
}

int Frame::CurrentStokes() {
    return _stokes_index;
}

bool Frame::CheckChannel(int channel) {
    return ((channel >= 0) && (channel < NumChannels()));
}

bool Frame::CheckStokes(int stokes) {
    return ((stokes >= 0) && (stokes < NumStokes()));
}

bool Frame::ChannelsChanged(int channel, int stokes) {
    return (channel != _channel_index || stokes != _stokes_index);
}

void Frame::DisconnectCalled() {
    _connected = false;        // file closed
    while (_z_profile_count) { // wait for spectral profiles to complete to avoid crash
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool Frame::IsConnected() {
    return _connected; // whether file is to be closed
}

// ********************************************************************
// Image parameters: view, channel/stokes, slicers for data cache

bool Frame::SetImageChannels(int new_channel, int new_stokes, std::string& message) {
    bool updated(false);

    if (!_valid) {
        message = "No file loaded";
    } else {
        if ((new_channel != _channel_index) || (new_stokes != _stokes_index)) {
            bool chan_ok(CheckChannel(new_channel));
            bool stokes_ok(CheckStokes(new_stokes));
            if (chan_ok && stokes_ok) {
                _channel_index = new_channel;
                _stokes_index = new_stokes;
                FillImageCache();
                updated = true;
            } else {
                message = fmt::format("Channel {} or Stokes {} is invalid in image", new_channel, new_stokes);
            }
        }
    }
    return updated;
}

bool Frame::SetCursor(float x, float y) {
    bool changed = ((x != _cursor.x) || (y != _cursor.y));
    _cursor = PointXy(x, y);
    return changed;
}

bool Frame::FillImageCache() {
    // get image data for channel, stokes
    bool write_lock(true);
    tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
    try {
        _image_cache.resize(_image_shape(0) * _image_shape(1));
    } catch (std::bad_alloc& alloc_error) {
        Log(_session_id, "Could not allocate memory for image data.");
        return false;
    }

    casacore::Slicer section = GetChannelMatrixSlicer(_channel_index, _stokes_index);
    if (!GetSlicerData(section, _image_cache)) {
        Log(_session_id, "Loading image cache failed.");
        return false;
    }
    return true;
}

void Frame::GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes) {
    // fill matrix for given channel and stokes
    casacore::Slicer section = GetChannelMatrixSlicer(channel, stokes);
    GetSlicerData(section, chan_matrix);
}

casacore::Slicer Frame::GetChannelMatrixSlicer(size_t channel, size_t stokes) {
    // slicer for spectral and stokes axes to select channel, stokes
    casacore::IPosition count(_image_shape);
    casacore::IPosition start(_image_shape.size());
    start = 0;

    if (_spectral_axis >= 0) {
        start(_spectral_axis) = channel;
        count(_spectral_axis) = 1;
    }
    if (_stokes_axis >= 0) {
        start(_stokes_axis) = stokes;
        count(_stokes_axis) = 1;
    }
    // slicer for image data
    casacore::Slicer section(start, count);
    return section;
}

// ****************************************************
// Raster Data

bool Frame::GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter) {
    // apply bounds and downsample image cache
    if (!_valid || _image_cache.empty()) {
        return false;
    }

    const int x = bounds.x_min();
    const int y = bounds.y_min();
    const int req_height = bounds.y_max() - y;
    const int req_width = bounds.x_max() - x;

    // check bounds
    if ((req_height < 0) || (req_width < 0)) {
        return false;
    }
    if (_image_shape(1) < y + req_height || _image_shape(0) < x + req_width) {
        return false;
    }
    // check mip; cannot divide by zero
    if (mip <= 0) {
        return false;
    }

    // size returned vector
    size_t num_rows_region = std::ceil((float)req_height / mip);
    size_t row_length_region = std::ceil((float)req_width / mip);
    image_data.resize(num_rows_region * row_length_region);
    int num_image_columns = _image_shape(0);

    // read lock imageCache
    bool write_lock(false);
    tbb::queuing_rw_mutex::scoped_lock lock(_cache_mutex, write_lock);

    if (mean_filter && mip > 1) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
#pragma omp parallel for
        for (size_t j = 0; j < num_rows_region; ++j) {
            for (size_t i = 0; i != row_length_region; ++i) {
                float pixel_sum = 0;
                int pixel_count = 0;
                size_t image_row = y + (j * mip);
                for (size_t pixel_y = 0; pixel_y < mip; pixel_y++) {
                    if (image_row >= _image_shape(1)) {
                        continue;
                    }
                    size_t image_col = x + (i * mip);
                    for (size_t pixel_x = 0; pixel_x < mip; pixel_x++) {
                        if (image_col >= _image_shape(0)) {
                            continue;
                        }
                        float pix_val = _image_cache[(image_row * num_image_columns) + image_col];
                        if (std::isfinite(pix_val)) {
                            pixel_count++;
                            pixel_sum += pix_val;
                        }
                        image_col++;
                    }
                    image_row++;
                }
                image_data[j * row_length_region + i] = pixel_count ? pixel_sum / pixel_count : NAN;
            }
        }

    } else {
        // Nearest neighbour filtering
#pragma omp parallel for
        for (size_t j = 0; j < num_rows_region; ++j) {
            for (auto i = 0; i < row_length_region; i++) {
                auto image_row = y + j * mip;
                auto image_col = x + i * mip;
                image_data[j * row_length_region + i] = _image_cache[(image_row * num_image_columns) + image_col];
            }
        }
    }
    return true;
}

// Tile data
bool Frame::FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int channel, int stokes,
    CARTA::CompressionType compression_type, float compression_quality) {
    // Early exit if channel has changed
    if (ChannelsChanged(channel, stokes)) {
        return false;
    }
    raster_tile_data.set_channel(channel);
    raster_tile_data.set_stokes(stokes);
    raster_tile_data.set_compression_type(compression_type);
    raster_tile_data.set_compression_quality(compression_quality);

    if (raster_tile_data.tiles_size()) {
        raster_tile_data.clear_tiles();
    }

    CARTA::TileData* tile_ptr = raster_tile_data.add_tiles();
    tile_ptr->set_layer(tile.layer);
    tile_ptr->set_x(tile.x);
    tile_ptr->set_y(tile.y);

    std::vector<float> tile_image_data;
    int tile_width;
    int tile_height;
    if (GetRasterTileData(tile_image_data, tile, tile_width, tile_height)) {
        if (ChannelsChanged(channel, stokes)) {
            return false;
        }
        tile_ptr->set_width(tile_width);
        tile_ptr->set_height(tile_height);
        if (compression_type == CARTA::CompressionType::NONE) {
            tile_ptr->set_image_data(tile_image_data.data(), sizeof(float) * tile_image_data.size());
            return true;
        } else if (compression_type == CARTA::CompressionType::ZFP) {
            auto nan_encodings = GetNanEncodingsBlock(tile_image_data, 0, tile_width, tile_height);
            tile_ptr->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());

            if (ChannelsChanged(channel, stokes)) {
                return false;
            }

            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(tile_image_data, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            tile_ptr->set_image_data(compression_buffer.data(), compressed_size);

            return !(ChannelsChanged(channel, stokes));
        }
    }
    return false;
}

bool Frame::GetRasterTileData(std::vector<float>& tile_data, const Tile& tile, int& width, int& height) {
    int tile_size = 256;
    int mip = Tile::LayerToMip(tile.layer, _image_shape(0), _image_shape(1), tile_size, tile_size);
    int tile_size_original = tile_size * mip;
    CARTA::ImageBounds bounds;
    // crop to image size
    bounds.set_x_min(std::max(0, tile.x * tile_size_original));
    bounds.set_x_max(std::min((int)_image_shape(0), (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::max(0, tile.y * tile_size_original));
    bounds.set_y_max(std::min((int)_image_shape(1), (tile.y + 1) * tile_size_original));

    const int req_height = bounds.y_max() - bounds.y_min();
    const int req_width = bounds.x_max() - bounds.x_min();
    width = std::ceil((float)req_width / mip);
    height = std::ceil((float)req_height / mip);
    return GetRasterData(tile_data, bounds, mip, true);
}

// ****************************************************
// Contour Data

bool Frame::SetContourParameters(const CARTA::SetContourParameters& message) {
    ContourSettings new_settings = {std::vector<double>(message.levels().begin(), message.levels().end()), message.smoothing_mode(),
        message.smoothing_factor(), message.decimation_factor(), message.compression_level(), message.contour_chunk_size(),
        message.reference_file_id()};

    if (_contour_settings != new_settings) {
        _contour_settings = new_settings;
        return true;
    }
    return false;
}

bool Frame::ContourImage(ContourCallback& partial_contour_callback) {
    double scale = 1.0;
    double offset = 0;
    bool smooth_successful = false;
    std::vector<std::vector<float>> vertex_data;
    std::vector<std::vector<int>> index_data;
    tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, false);

    if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::NoSmoothing || _contour_settings.smoothing_factor <= 1) {
        TraceContours(_image_cache.data(), _image_shape(0), _image_shape(1), scale, offset, _contour_settings.levels, vertex_data,
            index_data, _contour_settings.chunk_size, partial_contour_callback, _verbose);
        return true;
    } else if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::GaussianBlur) {
        // Smooth the image from cache
        int mask_size = (_contour_settings.smoothing_factor - 1) * 2 + 1;
        int64_t kernel_width = (mask_size - 1) / 2;

        int64_t source_width = _image_shape(0);
        int64_t source_height = _image_shape(1);
        int64_t dest_width = _image_shape(0) - 2 * kernel_width;
        int64_t dest_height = _image_shape(1) - 2 * kernel_width;
        std::unique_ptr<float[]> dest_array(new float[dest_width * dest_height]);
        smooth_successful = GaussianSmooth(_image_cache.data(), dest_array.get(), source_width, source_height, dest_width, dest_height,
            _contour_settings.smoothing_factor, _verbose);
        // Can release lock early, as we're no longer using the image cache
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the Gaussian smoothing apron size
            offset = _contour_settings.smoothing_factor - 1;
            TraceContours(dest_array.get(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback, _verbose);
            return true;
        }
    } else {
        // Block averaging
        CARTA::ImageBounds image_bounds;
        image_bounds.set_x_min(0);
        image_bounds.set_y_min(0);
        image_bounds.set_x_max(_image_shape(0));
        image_bounds.set_y_max(_image_shape(1));
        std::vector<float> dest_vector;
        smooth_successful = GetRasterData(dest_vector, image_bounds, _contour_settings.smoothing_factor, true);
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the block size, and a scale factor equal to block size
            offset = 0;
            scale = _contour_settings.smoothing_factor;
            size_t dest_width = ceil(double(image_bounds.x_max()) / _contour_settings.smoothing_factor);
            size_t dest_height = ceil(double(image_bounds.y_max()) / _contour_settings.smoothing_factor);
            TraceContours(dest_vector.data(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback, _verbose);
            return true;
        }
        fmt::print("Smoothing mode not implemented yet!\n");
        return false;
    }
}

// ****************************************************
// Histogram Requirements and Data

bool Frame::SetHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_configs) {
    // Set histogram requirements for image or cube
    if ((region_id > IMAGE_REGION_ID) || (region_id < CUBE_REGION_ID)) { // does not handle other regions
        return false;
    }

    if (region_id == IMAGE_REGION_ID) {
        _image_histogram_configs.clear();
    } else {
        _cube_histogram_configs.clear();
    }

    for (auto& histogram_config : histogram_configs) {
        HistogramConfig config;
        config.channel = histogram_config.channel();
        config.num_bins = histogram_config.num_bins();
        if (region_id == IMAGE_REGION_ID) {
            _image_histogram_configs.push_back(config);
        } else {
            _cube_histogram_configs.push_back(config);
        }
    }

    return true;
}

bool Frame::FillRegionHistogramData(int region_id, CARTA::RegionHistogramData& histogram_data) {
    // fill histogram message for image plane or cube
    if ((region_id > IMAGE_REGION_ID) || (region_id < CUBE_REGION_ID)) { // does not handle other regions
        return false;
    }
    // Use number of bins in requirements
    int stokes(CurrentStokes());

    // fill common message fields
    histogram_data.set_region_id(region_id);
    histogram_data.set_stokes(stokes);
    histogram_data.set_progress(1.0);

    std::vector<HistogramConfig> requirements;
    if (region_id == IMAGE_REGION_ID) {
        requirements = _image_histogram_configs;
    } else {
        requirements = _cube_histogram_configs;
    }

    bool have_valid_histogram(false);
    for (auto& histogram_config : requirements) {
        // set value for single channel
        int channel = histogram_config.channel;
        if ((channel == CURRENT_CHANNEL) || (NumChannels() == 1)) {
            channel = CurrentChannel();
        }
        int num_bins = histogram_config.num_bins;

        // Histogram submessage for this config
        auto histogram = histogram_data.add_histograms();
        histogram->set_channel(channel);

        // fill histogram submessage from cache (loader or local)
        bool histogram_filled = FillHistogramFromCache(channel, stokes, num_bins, histogram);

        if (!histogram_filled) {
            // must calculate cube histogram from Session
            if ((region_id == CUBE_REGION_ID) || (channel == ALL_CHANNELS)) {
                return false;
            }

            // calculate image histogram
            BasicStats<float> stats;
            if (GetBasicStats(channel, stokes, stats)) {
                HistogramResults results;
                histogram_filled = CalculateHistogram(region_id, channel, stokes, num_bins, stats, results);
                if (histogram_filled) {
                    FillHistogramFromResults(histogram, stats, results);
                }
            }
        }
        have_valid_histogram |= histogram_filled;
    }

    return have_valid_histogram; // true if any histograms filled
}

int Frame::AutoBinSize() {
    return int(std::max(sqrt(_image_shape(0) * _image_shape(1)), 2.0));
}

bool Frame::FillHistogramFromCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Fill Histogram submessage for given channel, stokes, and num_bins
    bool filled = FillHistogramFromLoaderCache(channel, stokes, num_bins, histogram);
    if (!filled) {
        filled = FillHistogramFromFrameCache(channel, stokes, num_bins, histogram);
    }
    return filled;
}

bool Frame::FillHistogramFromLoaderCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Fill the Histogram submessage from the loader cache
    auto& current_stats = _loader->GetImageStats(stokes, channel);
    if (current_stats.valid) {
        int image_num_bins(current_stats.histogram_bins.size());
        if ((num_bins == AUTO_BIN_SIZE) || (num_bins == image_num_bins)) {
            double min_val(current_stats.basic_stats[CARTA::StatsType::Min]);
            double max_val(current_stats.basic_stats[CARTA::StatsType::Max]);
            double mean(current_stats.basic_stats[CARTA::StatsType::Mean]);
            double std_dev(current_stats.basic_stats[CARTA::StatsType::Sigma]);

            // fill message
            histogram->set_num_bins(image_num_bins);
            histogram->set_bin_width((max_val - min_val) / image_num_bins);
            histogram->set_first_bin_center(min_val + (histogram->bin_width() / 2.0));
            *histogram->mutable_bins() = {current_stats.histogram_bins.begin(), current_stats.histogram_bins.end()};
            histogram->set_mean(mean);
            histogram->set_std_dev(std_dev);
            // histogram cached in loader
            return true;
        }
    }
    return false;
}

bool Frame::FillHistogramFromFrameCache(int channel, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Get stats and histogram results from cache; also used for cube histogram
    if (num_bins == AUTO_BIN_SIZE) {
        num_bins = AutoBinSize();
    }

    bool have_histogram(false);
    HistogramResults histogram_results;
    if (channel == CURRENT_CHANNEL) {
        have_histogram = GetCachedImageHistogram(channel, stokes, num_bins, histogram_results);
    } else if (channel == ALL_CHANNELS) {
        have_histogram = GetCachedCubeHistogram(stokes, num_bins, histogram_results);
    }

    if (have_histogram) {
        // add stats to message
        BasicStats<float> stats;
        if (GetBasicStats(channel, stokes, stats)) {
            FillHistogramFromResults(histogram, stats, histogram_results);
        }
    }
    return have_histogram;
}

bool Frame::GetBasicStats(int channel, int stokes, carta::BasicStats<float>& stats) {
    // Return basic stats from cache, or calculate (no loader option); also used for cube histogram
    if (channel == ALL_CHANNELS) { // cube
        if (_cube_basic_stats.count(stokes)) {
            stats = _cube_basic_stats[stokes]; // get from cache
            return true;
        }
        return false; // calculate and cache in Session
    } else {
        int index(ChannelStokesIndex(channel, stokes));
        if (_image_basic_stats.count(index)) {
            stats = _image_basic_stats[index]; // get from cache
            return true;
        }

        if ((channel == CurrentChannel()) && (stokes == CurrentStokes())) {
            // calculate histogram from image cache
            if (_image_cache.empty() && !FillImageCache()) {
                // cannot calculate
                return false;
            }
            CalcBasicStats(_image_cache, stats);
            _image_basic_stats[index] = stats;
            return true;
        }

        // calculate histogram from given chan/stokes data
        std::vector<float> data;
        GetChannelMatrix(data, channel, stokes);
        CalcBasicStats(data, stats);
        _image_basic_stats[index] = stats;
        return true;
    }
    return false;
}

bool Frame::GetCachedImageHistogram(int channel, int stokes, int num_bins, HistogramResults& histogram_results) {
    // Get image histogram results from cache
    int index(ChannelStokesIndex(channel, stokes));
    if (_image_histograms.count(index)) {
        // get from cache if correct num_bins
        auto results_for_index = _image_histograms[index];
        for (auto& result : results_for_index) {
            if (result.num_bins == num_bins) {
                histogram_results = result;
                return true;
            }
        }
    }
    return false;
}

bool Frame::GetCachedCubeHistogram(int stokes, int num_bins, HistogramResults& histogram_results) {
    // Get cube histogram results from cache
    if (_cube_histograms.count(stokes)) {
        for (auto& result : _cube_histograms[stokes]) {
            // get from cache if correct num_bins
            if (result.num_bins == num_bins) {
                histogram_results = result;
                return true;
            }
        }
    }
    return false;
}

bool Frame::CalculateHistogram(int region_id, int channel, int stokes, int num_bins, BasicStats<float>& stats, HistogramResults& results) {
    // Calculate histogram for given parameters, return results
    if ((region_id > IMAGE_REGION_ID) || (region_id < CUBE_REGION_ID)) { // does not handle other regions
        return false;
    }

    if (channel == ALL_CHANNELS) {
        return false; // calculation only for a specific channel, even for cube histograms
    }

    if (num_bins == AUTO_BIN_SIZE) {
        num_bins = AutoBinSize();
    }

    if ((channel == CurrentChannel()) && (stokes == CurrentStokes())) {
        // calculate histogram from current image cache
        if (_image_cache.empty() && !FillImageCache()) {
            return false;
        }
        bool write_lock(false);
        tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
        CalcHistogram(num_bins, stats, _image_cache, results);
    } else {
        // calculate histogram for chan/stokes data
        std::vector<float> data;
        GetChannelMatrix(data, channel, stokes);
        CalcHistogram(num_bins, stats, data, results);
    }

    // cache image histogram
    if ((region_id == IMAGE_REGION_ID) || (NumChannels() == 1)) {
        int index(ChannelStokesIndex(channel, stokes));
        _image_histograms[index].push_back(results);
    }

    return true;
}

bool Frame::GetCubeHistogramConfig(HistogramConfig& config) {
    bool have_config(!_cube_histogram_configs.empty());
    if (have_config) {
        config = _cube_histogram_configs[0];
    }
    return have_config;
}

void Frame::CacheCubeStats(int stokes, carta::BasicStats<float>& stats) {
    _cube_basic_stats[stokes] = stats;
}

void Frame::CacheCubeHistogram(int stokes, carta::HistogramResults& results) {
    _cube_histograms[stokes].push_back(results);
}

// ****************************************************
// Stats Requirements and Data

bool Frame::SetStatsRequirements(int region_id, const std::vector<int>& stats_types) {
    if (region_id != IMAGE_REGION_ID) {
        return false;
    }

    _image_required_stats = stats_types;
    return true;
}

bool Frame::FillRegionStatsData(int region_id, CARTA::RegionStatsData& stats_data) {
    // fill stats data message with requested statistics for the region with current channel and stokes
    if (region_id != IMAGE_REGION_ID) {
        return false;
    }

    if (_image_required_stats.empty()) {
        return false; // not requested
    }

    int channel(CurrentChannel()), stokes(CurrentStokes());
    stats_data.set_channel(channel);
    stats_data.set_stokes(stokes);

    // Use loader image stats
    auto& image_stats = _loader->GetImageStats(stokes, channel);
    if (image_stats.full) {
        FillStatisticsValuesFromMap(stats_data, _image_required_stats, image_stats.basic_stats);
        return true;
    }

    // Use cached stats
    int index(ChannelStokesIndex(channel, stokes));
    if (_image_stats.count(index)) {
        auto stats_map = _image_stats[index];
        FillStatisticsValuesFromMap(stats_data, _image_required_stats, stats_map);
        return true;
    }

    // Calculate stats map using slicer
    casacore::Slicer slicer = GetChannelMatrixSlicer(channel, stokes);
    bool per_channel(false);
    std::map<CARTA::StatsType, std::vector<double>> stats_vector_map;
    if (GetSlicerStats(slicer, _image_required_stats, per_channel, stats_vector_map)) {
        // convert vector to single value in map
        std::map<CARTA::StatsType, double> stats_map;
        for (auto& value : stats_vector_map) {
            stats_map[value.first] = value.second[0];
        }

        // complete message
        FillStatisticsValuesFromMap(stats_data, _image_required_stats, stats_map);

        // cache results
        _image_stats[index] = stats_map;
        return true;
    }
    return false;
}

// ****************************************************
// Spatial Requirements and Data

bool Frame::SetSpatialRequirements(int region_id, const std::vector<std::string>& spatial_profiles) {
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    _cursor_spatial_configs.clear();
    for (auto& profile : spatial_profiles) {
        _cursor_spatial_configs.push_back(profile);
    }
    return true;
}

bool Frame::FillSpatialProfileData(int region_id, CARTA::SpatialProfileData& spatial_data) {
    // Fill spatial profile message for cursor only
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    // No spatial profile requirements
    if (_cursor_spatial_configs.empty()) {
        return false;
    }

    // frontend does not set cursor outside of image, but just in case:
    if (!_cursor.InImage(_image_shape(0), _image_shape(1))) {
        return false;
    }

    ssize_t num_image_cols(_image_shape(0)), num_image_rows(_image_shape(1));
    int x, y;
    _cursor.ToIndex(x, y); // convert float to index into image array
    float cursor_value(0.0);
    if (!_image_cache.empty()) {
        bool write_lock(false);
        tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
        cursor_value = _image_cache[(y * num_image_cols) + x];
        cache_lock.release();
    }

    // set message fields
    spatial_data.set_x(x);
    spatial_data.set_y(y);
    spatial_data.set_channel(CurrentChannel());
    spatial_data.set_stokes(CurrentStokes());
    spatial_data.set_value(cursor_value);

    // add profiles
    int end(0);
    std::vector<float> profile;
    bool write_lock(false);
    for (auto& coordinate : _cursor_spatial_configs) { // string coordinate
        bool have_profile(false);
        // can no longer select stokes, so can use image cache
        if (coordinate == "x") {
            tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
            auto x_start = y * num_image_cols;
            profile.reserve(_image_shape(0));
            for (unsigned int j = 0; j < _image_shape(0); ++j) {
                auto idx = x_start + j;
                profile.push_back(_image_cache[idx]);
            }
            cache_lock.release();
            end = _image_shape(0);
            have_profile = true;
        } else if (coordinate == "y") {
            tbb::queuing_rw_mutex::scoped_lock cache_lock(_cache_mutex, write_lock);
            profile.reserve(_image_shape(1));
            for (unsigned int j = 0; j < _image_shape(1); ++j) {
                auto idx = (j * num_image_cols) + x;
                profile.push_back(_image_cache[idx]);
            }
            cache_lock.release();
            end = _image_shape(1);
            have_profile = true;
        }

        if (have_profile) {
            // add SpatialProfile to message
            auto spatial_profile = spatial_data.add_profiles();
            spatial_profile->set_coordinate(coordinate);
            spatial_profile->set_start(0);
            spatial_profile->set_end(end);
            spatial_profile->set_raw_values_fp32(profile.data(), profile.size() * sizeof(float));
        }
    }

    return (spatial_data.profiles_size() > 0);
}

// ****************************************************
// Spectral Requirements and Data

bool Frame::SetSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_configs) {
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    // frontend does not set cursor outside of image, but just in case:
    _cursor_spectral_configs.clear();
    for (auto& config : spectral_configs) {
        std::string coordinate(config.coordinate());
        int axis, stokes;
        ConvertCoordinateToAxes(coordinate, axis, stokes);
        std::vector<int> stats = std::vector<int>(config.stats_types().begin(), config.stats_types().end());
        SpectralConfig new_config(coordinate, stokes, stats);
        _cursor_spectral_configs.push_back(new_config);
    }
    return true;
}

bool Frame::FillSpectralProfileData(std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, bool stokes_changed) {
    // Send cursor profile data incrementally using callback cb
    // If fixed stokes requirement and stokes changed, do not send that profile
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    // No spectral axis
    if (_spectral_axis < 0) {
        return false;
    }

    // No spectral profile requirements
    if (_cursor_spectral_configs.empty()) {
        return false;
    }

    PointXy start_cursor = _cursor; // if cursor changes, cancel profiles

    for (auto& config : _cursor_spectral_configs) { // config is SpectralConfig struct
        if (!(_cursor == start_cursor) || !IsConnected()) {
            // cursor changed or file closed, cancel profiles
            return false;
        }

        int config_stokes = config.stokes_index;
        if ((config_stokes != CURRENT_STOKES) && stokes_changed) {
            continue; // do not resend fixed stokes profile when stokes changes
        }

        // Create final profile message for callback
        CARTA::SpectralProfileData profile_message;
        profile_message.set_stokes(CurrentStokes());
        profile_message.set_progress(1.0);
        auto spectral_profile = profile_message.add_profiles();
        spectral_profile->set_coordinate(config.coordinate);
        // point spectral profiles only have one stats type
        CARTA::StatsType stats_type = static_cast<CARTA::StatsType>(config.stats_types[0]);
        spectral_profile->set_stats_type(stats_type);

        // Send NaN if cursor outside image
        if (!start_cursor.InImage(_image_shape(0), _image_shape(1))) {
            double nan_value = std::numeric_limits<double>::quiet_NaN();
            spectral_profile->set_raw_values_fp64(&nan_value, sizeof(double));
            cb(profile_message);
        } else {
            int stokes = (config_stokes == CURRENT_STOKES ? CurrentStokes() : config_stokes);

            std::vector<float> spectral_data;
            int xy_count(1);
            if (_loader->GetCursorSpectralData(
                    spectral_data, stokes, (start_cursor.x + 0.5), xy_count, (start_cursor.y + 0.5), xy_count, _image_mutex)) {
                // Use loader data
                spectral_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                cb(profile_message);
            } else {
                // Send image slices
                // Set up slicer
                int x_index, y_index;
                start_cursor.ToIndex(x_index, y_index);
                casacore::IPosition start(_image_shape.size());
                start(0) = x_index;
                start(1) = y_index;
                start(_spectral_axis) = 0;
                if (_stokes_axis >= 0) {
                    start(_stokes_axis) = stokes;
                }
                casacore::IPosition count(_image_shape.size(), 1); // will adjust count for spectral axis

                // Send incremental spectral profile when reach delta channel or delta time
                size_t delta_channels = INIT_DELTA_CHANNEL;             // the increment of channels for each step
                size_t dt_target = TARGET_DELTA_TIME;                   // the target time elapse for each step, in milliseconds
                size_t dt_partial_profile = TARGET_PARTIAL_CURSOR_TIME; // time increment to send an update
                size_t profile_size = NumChannels();                    // profile vector size
                spectral_data.resize(profile_size, std::numeric_limits<float>::quiet_NaN());
                float progress(0.0);

                while (progress < PROFILE_COMPLETE) {
                    if (!(_cursor == start_cursor) || !IsConnected()) { // cursor changed or file closed, cancel profiles
                        return false;
                    }

                    // start timer for slice
                    auto t_start = std::chrono::high_resolution_clock::now();

                    // Slice image to get next delta_channels (not to exceed number of channels in image)
                    size_t nchan =
                        (start(_spectral_axis) + delta_channels < profile_size ? delta_channels : profile_size - start(_spectral_axis));
                    count(_spectral_axis) = nchan;
                    casacore::Slicer slicer(start, count);
                    std::vector<float> buffer;
                    if (!GetSlicerData(slicer, buffer)) {
                        return false;
                    }

                    // copy buffer to spectral_data
                    memcpy(&spectral_data[start(_spectral_axis)], buffer.data(), nchan * sizeof(float));

                    // update start channel and determine progress
                    start(_spectral_axis) += nchan;
                    progress = (float)start(_spectral_axis) / profile_size;

                    // get the time elapse for this slice
                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();

                    // adjust the increment of channels per slice according to the time elapse
                    if (delta_channels == INIT_DELTA_CHANNEL) {
                        delta_channels *= dt_target / dt;
                        if (delta_channels < 1) {
                            delta_channels = 1;
                        }
                        if (delta_channels > profile_size) {
                            delta_channels = profile_size;
                        }
                    }

                    if (progress >= PROFILE_COMPLETE) {
                        spectral_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                        // send final profile message
                        cb(profile_message);
                    } else if (dt > dt_partial_profile) {
                        CARTA::SpectralProfileData partial_data;
                        partial_data.set_stokes(CurrentStokes());
                        partial_data.set_progress(progress);
                        auto partial_profile = partial_data.add_profiles();
                        partial_profile->set_stats_type(stats_type);
                        partial_profile->set_coordinate(config.coordinate);
                        partial_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                        // send partial profile message
                        cb(partial_data);
                    }
                }
            }
        }
    }
    return true;
}

void Frame::IncreaseZProfileCount() {
    ++_z_profile_count;
}

void Frame::DecreaseZProfileCount() {
    --_z_profile_count;
}

// ****************************************************
// Region/Slicer Support (Frame manages image mutex)

casacore::IPosition Frame::GetRegionShape(const casacore::LattRegionHolder& region) {
    // Returns image shape with a region applied
    casacore::IPosition ipos;

    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(region, sub_image);
    ulock.unlock();

    if (subimage_ok) {
        ipos = sub_image.shape();
    }
    return ipos;
}

bool Frame::GetRegionData(const casacore::LattRegionHolder& region, std::vector<float>& data) {
    // Get image data with a region applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(region, sub_image);
    ulock.unlock();

    if (!subimage_ok) {
        return false;
    }

    casacore::IPosition subimage_shape = sub_image.shape();
    if (subimage_shape.empty()) {
        return false;
    }

    data.resize(subimage_shape.product());
    casacore::Array<float> tmp(subimage_shape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        casacore::Slicer slicer; // entire subimage
        sub_image.doGetSlice(tmp, slicer);
        return true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }

    return false;
}

bool Frame::GetSlicerData(const casacore::Slicer& slicer, std::vector<float>& data) {
    // Get image data with a slicer applied
    casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool data_ok = _loader->GetSlice(tmp, slicer);
    ulock.unlock();
    return data_ok;
}

bool Frame::GetRegionStats(const casacore::LattRegionHolder& region, std::vector<int>& required_stats, bool per_channel,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a region applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(region, sub_image);
    ulock.unlock();
    if (subimage_ok) {
        return CalcStatsValues(stats_values, required_stats, sub_image, per_channel);
    }
    return subimage_ok;
}

bool Frame::GetSlicerStats(const casacore::Slicer& slicer, std::vector<int>& required_stats, bool per_channel,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a slicer applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(slicer, sub_image);
    ulock.unlock();
    if (subimage_ok) {
        return CalcStatsValues(stats_values, required_stats, sub_image, per_channel);
    }
    return subimage_ok;
}
