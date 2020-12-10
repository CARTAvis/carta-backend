/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Frame.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>
#include "fmt/format.h"

#include <casacore/images/Regions/WCBox.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/tables/DataMan/TiledFileAccess.h>

#include "DataStream/Compression.h"
#include "DataStream/Contouring.h"
#include "DataStream/Smoothing.h"
#include "ImageStats/StatsCalculator.h"
#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace carta;

Frame::Frame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, bool verbose, bool perflog, int default_channel)
    : _session_id(session_id),
      _verbose(verbose),
      _perflog(perflog),
      _valid(true),
      _loader(loader),
      _spectral_axis(-1),
      _stokes_axis(-1),
      _channel_index(-1),
      _stokes_index(-1),
      _num_channels(1),
      _num_stokes(1),
      _z_profile_count(0),
      _moments_count(0),
      _moment_generator(nullptr) {
    if (!_loader) {
        _open_image_error = fmt::format("Problem loading image: image type not supported.");
        if (_verbose) {
            carta::Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    try {
        _loader->OpenFile(hdu);
    } catch (casacore::AipsError& err) {
        _open_image_error = err.getMesg();
        if (_verbose) {
            carta::Log(session_id, _open_image_error);
        }
        _valid = false;
        return;
    }

    // Get shape and axis values from the loader
    std::string log_message;
    if (!_loader->FindCoordinateAxes(_image_shape, _spectral_axis, _stokes_axis, log_message)) {
        _open_image_error = fmt::format("Problem determining file shape: {}", log_message);
        if (_verbose) {
            carta::Log(session_id, _open_image_error);
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
            carta::Log(session_id, _open_image_error);
        }
    }
}

bool Frame::IsValid() {
    return _valid;
}

std::string Frame::GetErrorMessage() {
    return _open_image_error;
}

casacore::CoordinateSystem* Frame::CoordinateSystem() {
    // Returns pointer to CoordinateSystem clone; caller must delete
    casacore::CoordinateSystem* csys(nullptr);
    if (IsValid()) {
        std::lock_guard<std::mutex> guard(_image_mutex);
        casacore::CoordinateSystem image_csys;
        _loader->GetCoordinateSystem(image_csys);
        csys = static_cast<casacore::CoordinateSystem*>(image_csys.clone());
    }
    return csys;
}

casacore::IPosition Frame::ImageShape() {
    casacore::IPosition ipos;
    if (IsValid()) {
        ipos = _image_shape;
    }
    return ipos;
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

int Frame::StokesAxis() {
    return _stokes_axis;
}

casacore::Slicer Frame::GetImageSlicer(const ChannelRange& chan_range, int stokes) {
    // Slicer to apply channel range and stokes to image shape

    // Normalize channel and stokes constants
    int start_chan(chan_range.from), end_chan(chan_range.to);
    if (start_chan == ALL_CHANNELS) {
        start_chan = 0;
    } else if (start_chan == CURRENT_CHANNEL) {
        start_chan = CurrentChannel();
    }
    if (end_chan == ALL_CHANNELS) {
        end_chan = NumChannels();
    } else if (end_chan == CURRENT_CHANNEL) {
        end_chan = CurrentChannel();
    }
    stokes = (stokes == CURRENT_STOKES ? CurrentStokes() : stokes);

    // Start with entire image
    casacore::IPosition start(_image_shape.size());
    start = 0;
    casacore::IPosition end(_image_shape);
    end -= 1; // last position, not length

    // Set channel and stokes axis ranges
    if (_spectral_axis >= 0) {
        start(_spectral_axis) = start_chan;
        end(_spectral_axis) = end_chan;
    }
    if (_stokes_axis >= 0) {
        start(_stokes_axis) = stokes;
        end(_stokes_axis) = stokes;
    }

    // slicer for image data
    casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
    return section;
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
    _connected = false;      // file closed
    if (_moment_generator) { // stop moment calculation
        _moment_generator->StopCalculation();
    }
    while (_z_profile_count > 0 || _moments_count > 0) { // wait for spectral profiles or moments calculation finished
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool Frame::IsConnected() {
    return _connected; // whether file is to be closed
}

void Frame::IncreaseZProfileCount() {
    _z_profile_count++;
}

void Frame::DecreaseZProfileCount() {
    _z_profile_count--;
}

void Frame::IncreaseMomentsCount() {
    _moments_count++;
}

void Frame::DecreaseMomentsCount() {
    _moments_count--;
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
    auto t_start_set_image_cache = std::chrono::high_resolution_clock::now();
    casacore::Slicer section = GetImageSlicer(ChannelRange(_channel_index), _stokes_index);
    if (!GetSlicerData(section, _image_cache)) {
        carta::Log(_session_id, "Loading image cache failed.");
        return false;
    }

    if (_perflog) {
        auto t_end_set_image_cache = std::chrono::high_resolution_clock::now();
        auto dt_set_image_cache =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_set_image_cache - t_start_set_image_cache).count();
        fmt::print("Load {}x{} image to cache in {} ms at {} MPix/s\n", _image_shape(0), _image_shape(1), dt_set_image_cache * 1e-3,
            (float)(_image_shape(0) * _image_shape(1)) / dt_set_image_cache);
    }

    return true;
}

void Frame::GetChannelMatrix(std::vector<float>& chan_matrix, size_t channel, size_t stokes) {
    // fill matrix for given channel and stokes
    casacore::Slicer section = GetImageSlicer(ChannelRange(channel), stokes);
    GetSlicerData(section, chan_matrix);
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

    auto t_start_raster_data_filter = std::chrono::high_resolution_clock::now();
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

    if (_perflog) {
        auto t_end_raster_data_filter = std::chrono::high_resolution_clock::now();
        auto dt_raster_data_filter =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_raster_data_filter - t_start_raster_data_filter).count();
        fmt::print("{} filter {}x{} raster data to {}x{} in {} ms at {} MPix/s\n", (mean_filter && mip > 1) ? "Mean" : "Nearest neighbour",
            req_height, req_width, num_rows_region, row_length_region, dt_raster_data_filter * 1e-3,
            (float)(num_rows_region * row_length_region) / dt_raster_data_filter);
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

    auto t_start_compress_tile_data = std::chrono::high_resolution_clock::now();

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

            auto t_start_compress_tile_data = std::chrono::high_resolution_clock::now();
            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(tile_image_data, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            tile_ptr->set_image_data(compression_buffer.data(), compressed_size);
            // Measure duration for compress tile data
            if (_perflog) {
                auto t_end_compress_tile_data = std::chrono::high_resolution_clock::now();
                auto dt_compress_tile_data =
                    std::chrono::duration_cast<std::chrono::microseconds>(t_end_compress_tile_data - t_start_compress_tile_data).count();
                fmt::print("Compress {}x{} tile data in {} ms at {} MPix/s\n", tile_width, tile_height, dt_compress_tile_data * 1e-3,
                    (float)(tile_width * tile_height) / dt_compress_tile_data);
            }

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
            index_data, _contour_settings.chunk_size, partial_contour_callback, _perflog);
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
            _contour_settings.smoothing_factor, _perflog);
        // Can release lock early, as we're no longer using the image cache
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the Gaussian smoothing apron size
            offset = _contour_settings.smoothing_factor - 1;
            TraceContours(dest_array.get(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback, _perflog);
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
                _contour_settings.chunk_size, partial_contour_callback, _perflog);
            return true;
        }
        fmt::print("Smoothing mode not implemented yet!\n");
        return false;
    }

    return false;
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
        auto t_start_image_histogram = std::chrono::high_resolution_clock::now();

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

            if (_perflog && histogram_filled) {
                auto t_end_image_histogram = std::chrono::high_resolution_clock::now();
                auto dt_image_histogram =
                    std::chrono::duration_cast<std::chrono::microseconds>(t_end_image_histogram - t_start_image_histogram).count();
                fmt::print("Fill image histogram in {} ms at {} MPix/s\n", dt_image_histogram * 1e-3,
                    (float)stats.num_pixels / dt_image_histogram);
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
        int cache_key(CacheKey(channel, stokes));
        if (_image_basic_stats.count(cache_key)) {
            stats = _image_basic_stats[cache_key]; // get from cache
            return true;
        }

        if ((channel == CurrentChannel()) && (stokes == CurrentStokes())) {
            // calculate histogram from image cache
            if (_image_cache.empty() && !FillImageCache()) {
                // cannot calculate
                return false;
            }
            CalcBasicStats(_image_cache, stats);
            _image_basic_stats[cache_key] = stats;
            return true;
        }

        // calculate histogram from given chan/stokes data
        std::vector<float> data;
        GetChannelMatrix(data, channel, stokes);
        CalcBasicStats(data, stats);

        // cache results
        _image_basic_stats[cache_key] = stats;
        return true;
    }
    return false;
}

bool Frame::GetCachedImageHistogram(int channel, int stokes, int num_bins, HistogramResults& histogram_results) {
    // Get image histogram results from cache
    int cache_key(CacheKey(channel, stokes));
    if (_image_histograms.count(cache_key)) {
        // get from cache if correct num_bins
        auto results_for_key = _image_histograms[cache_key];

        for (auto& result : results_for_key) {
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
        int cache_key(CacheKey(channel, stokes));
        _image_histograms[cache_key].push_back(results);
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

bool Frame::SetStatsRequirements(int region_id, const std::vector<CARTA::StatsType>& stats_types) {
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
    int cache_key(CacheKey(channel, stokes));
    if (_image_stats.count(cache_key)) {
        auto stats_map = _image_stats[cache_key];
        FillStatisticsValuesFromMap(stats_data, _image_required_stats, stats_map);
        return true;
    }

    auto t_start_image_stats = std::chrono::high_resolution_clock::now();

    // Calculate stats map using slicer
    casacore::Slicer slicer = GetImageSlicer(ChannelRange(channel), stokes);
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
        _image_stats[cache_key] = stats_map;

        if (_perflog) {
            auto t_end_image_stats = std::chrono::high_resolution_clock::now();
            auto dt_image_stats = std::chrono::duration_cast<std::chrono::microseconds>(t_end_image_stats - t_start_image_stats).count();
            fmt::print("Fill image stats in {} ms\n", dt_image_stats * 1e-3);
        }
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
    // Send even if no requirements, to update value of data at cursor
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    // frontend does not set cursor outside of image, but just in case:
    if (!_cursor.InImage(_image_shape(0), _image_shape(1))) {
        return false;
    }

    auto t_start_spatial_profile = std::chrono::high_resolution_clock::now();

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
            profile.clear();
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
            profile.clear();
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

    if (_perflog) {
        auto t_end_spatial_profile = std::chrono::high_resolution_clock::now();
        auto dt_spatial_profile =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_spatial_profile - t_start_spatial_profile).count();
        fmt::print("Fill spatial profile in {} ms\n", dt_spatial_profile * 1e-3);
    }

    return true;
}

// ****************************************************
// Spectral Requirements and Data

bool Frame::SetSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_configs) {
    if (region_id != CURSOR_REGION_ID) {
        return false;
    }

    if (spectral_configs.empty()) {
        _cursor_spectral_configs.clear();
        return true;
    }

    int nstokes = NumStokes();
    std::vector<SpectralConfig> new_configs;
    for (auto& config : spectral_configs) {
        std::string coordinate(config.coordinate());
        int axis, stokes;
        ConvertCoordinateToAxes(coordinate, axis, stokes);
        if (stokes >= nstokes) {
            std::cerr << "Spectral requirement " << coordinate << " failed: invalid stokes axis for image." << std::endl;
            continue;
        }

        // Set required stats for coordinate
        size_t nstats = config.stats_types_size();
        std::vector<CARTA::StatsType> stats;
        for (size_t i = 0; i < config.stats_types_size(); ++i) {
            stats.push_back(config.stats_types(i));
        }
        SpectralConfig new_config(coordinate, stats);
        new_configs.push_back(new_config);
    }

    if (new_configs.empty()) {
        return false;
    }

    // Set cursor spectral config
    std::lock_guard<std::mutex> guard(_spectral_mutex);
    _cursor_spectral_configs = new_configs;
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

    IncreaseZProfileCount();
    PointXy start_cursor = _cursor; // if cursor changes, cancel profiles

    auto t_start_spectral_profile = std::chrono::high_resolution_clock::now();

    std::vector<SpectralConfig> current_configs;
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    current_configs.insert(current_configs.begin(), _cursor_spectral_configs.begin(), _cursor_spectral_configs.end());
    ulock.unlock();

    for (auto& config : current_configs) {
        if (!(_cursor == start_cursor) || !IsConnected()) {
            // cursor changed or file closed, cancel profiles
            DecreaseZProfileCount();
            return false;
        }
        if (!HasSpectralConfig(config)) {
            // requirements changed
            DecreaseZProfileCount();
            return false;
        }

        std::string coordinate(config.coordinate);
        if ((coordinate != "z") && stokes_changed) {
            continue; // do not send fixed stokes profile when stokes changes
        }

        // Create final profile message for callback
        CARTA::SpectralProfileData profile_message;
        profile_message.set_stokes(CurrentStokes());
        profile_message.set_progress(1.0);
        auto spectral_profile = profile_message.add_profiles();
        spectral_profile->set_coordinate(config.coordinate);
        // point spectral profiles only have one stats type
        spectral_profile->set_stats_type(config.all_stats[0]);

        // Send NaN if cursor outside image
        if (!start_cursor.InImage(_image_shape(0), _image_shape(1))) {
            double nan_value = std::numeric_limits<double>::quiet_NaN();
            spectral_profile->set_raw_values_fp64(&nan_value, sizeof(double));
            cb(profile_message);
        } else {
            int axis, stokes;
            ConvertCoordinateToAxes(coordinate, axis, stokes);
            if (coordinate == "z") {
                stokes = CurrentStokes();
            }

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
                size_t delta_channels = INIT_DELTA_CHANNEL;            // the increment of channels for each slice (to be adjusted)
                size_t dt_slice_target = TARGET_DELTA_TIME;            // target time elapse for each slice, in milliseconds
                size_t dt_partial_update = TARGET_PARTIAL_CURSOR_TIME; // time increment to send an update
                size_t profile_size = NumChannels();                   // profile vector size
                spectral_data.resize(profile_size, std::numeric_limits<float>::quiet_NaN());
                float progress(0.0);

                auto t_start_profile = std::chrono::high_resolution_clock::now();

                while (progress < PROFILE_COMPLETE) {
                    // start timer for slice
                    auto t_start_slice = std::chrono::high_resolution_clock::now();

                    // Slice image to get next delta_channels (not to exceed number of channels in image)
                    size_t nchan =
                        (start(_spectral_axis) + delta_channels < profile_size ? delta_channels : profile_size - start(_spectral_axis));
                    count(_spectral_axis) = nchan;
                    casacore::Slicer slicer(start, count);
                    std::vector<float> buffer;
                    if (!GetSlicerData(slicer, buffer)) {
                        DecreaseZProfileCount();
                        return false;
                    }

                    // copy buffer to spectral_data
                    memcpy(&spectral_data[start(_spectral_axis)], buffer.data(), nchan * sizeof(float));

                    // update start channel and determine progress
                    start(_spectral_axis) += nchan;
                    progress = (float)start(_spectral_axis) / profile_size;

                    // get the time elapse for this slice
                    auto t_end_slice = std::chrono::high_resolution_clock::now();
                    auto dt_slice = std::chrono::duration<double, std::milli>(t_end_slice - t_start_slice).count();
                    auto dt_profile = std::chrono::duration<double, std::milli>(t_end_slice - t_start_profile).count();

                    // adjust the number of channels per slice according to the time elapse,
                    // to achieve target elapsed time per slice TARGET_DELTA_TIME (used to check for cancel)
                    if (delta_channels == INIT_DELTA_CHANNEL) {
                        delta_channels *= dt_slice_target / dt_slice;
                        if (delta_channels < 1) {
                            delta_channels = 1;
                        }
                        if (delta_channels > profile_size) {
                            delta_channels = profile_size;
                        }
                    }

                    // Check for cancel before sending
                    if (!(_cursor == start_cursor) || !IsConnected()) { // cursor changed or file closed, cancel all profiles
                        DecreaseZProfileCount();
                        return false;
                    }
                    if (!HasSpectralConfig(config)) {
                        // requirements changed, cancel this profile
                        DecreaseZProfileCount();
                        break;
                    }

                    if (progress >= PROFILE_COMPLETE) {
                        spectral_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                        // send final profile message
                        cb(profile_message);
                    } else if (dt_profile > dt_partial_update) {
                        // reset profile timer and send partial profile message
                        t_start_profile = t_end_slice;

                        CARTA::SpectralProfileData partial_data;
                        partial_data.set_stokes(CurrentStokes());
                        partial_data.set_progress(progress);
                        auto partial_profile = partial_data.add_profiles();
                        partial_profile->set_stats_type(config.all_stats[0]);
                        partial_profile->set_coordinate(config.coordinate);
                        partial_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                        cb(partial_data);
                    }
                }
            }
        }
    }

    if (_perflog) {
        auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
        auto dt_spectral_profile =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
        fmt::print("Fill cursor spectral profile in {} ms\n", dt_spectral_profile * 1e-3);
    }

    DecreaseZProfileCount();
    return true;
}

bool Frame::HasSpectralConfig(const SpectralConfig& config) {
    // Check if requirement is still set.
    // Currently can only set stokes for cursor, do not check stats type
    std::vector<SpectralConfig> current_configs;
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    current_configs.insert(current_configs.begin(), _cursor_spectral_configs.begin(), _cursor_spectral_configs.end());
    ulock.unlock();
    for (auto& current_config : current_configs) {
        if (current_config.coordinate == config.coordinate) {
            return true;
        }
    }
    return false;
}

// ****************************************************
// Region/Slicer Support (Frame manages image mutex)

casacore::LCRegion* Frame::GetImageRegion(int file_id, std::shared_ptr<carta::Region> region) {
    // Return LCRegion formed by applying region params to image.
    // Returns nullptr if region outside image
    casacore::CoordinateSystem* coord_sys = CoordinateSystem();
    casacore::LCRegion* image_region = region->GetImageRegion(file_id, *coord_sys, ImageShape());
    delete coord_sys;
    return image_region;
}

bool Frame::GetImageRegion(int file_id, const ChannelRange& chan_range, int stokes, casacore::ImageRegion& image_region) {
    if (!CheckChannel(chan_range.from) || !CheckChannel(chan_range.to) || !CheckStokes(stokes)) {
        return false;
    }
    try {
        casacore::Slicer slicer = GetImageSlicer(chan_range, stokes);
        casacore::LCSlicer lcslicer(slicer);
        casacore::ImageRegion this_region(lcslicer);
        image_region = this_region;
        return true;
    } catch (casacore::AipsError error) {
        std::cerr << "Error converting full region to file " << file_id << ": " << error.getMesg() << std::endl;
        return false;
    }
}

casacore::IPosition Frame::GetRegionShape(const casacore::LattRegionHolder& region) {
    // Returns image shape with a region applied
    casacore::CoordinateSystem* coord_sys = CoordinateSystem();
    casacore::LatticeRegion lattice_region = region.toLatticeRegion(*coord_sys, ImageShape());
    delete coord_sys;
    return lattice_region.shape();
}

bool Frame::GetRegionData(const casacore::LattRegionHolder& region, std::vector<float>& data) {
    // Get image data with a region applied
    auto t_start_get_subimage_data = std::chrono::high_resolution_clock::now();
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

    data.resize(subimage_shape.product()); // must size correctly before sharing
    casacore::Array<float> tmp(subimage_shape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        casacore::IPosition start(subimage_shape.size(), 0);
        casacore::IPosition count(subimage_shape);
        casacore::Slicer slicer(start, count); // entire subimage
        std::unique_lock<std::mutex> ulock(_image_mutex);
        sub_image.doGetSlice(tmp, slicer);
        ulock.unlock();

        if (_perflog) {
            auto t_end_get_subimage_data = std::chrono::high_resolution_clock::now();
            auto dt_get_subimage_data =
                std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_subimage_data - t_start_get_subimage_data).count();
            fmt::print("Get region subimage data in {} ms\n", dt_get_subimage_data * 1e-3);
        }
        return true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }

    return false;
}

bool Frame::GetSlicerData(const casacore::Slicer& slicer, std::vector<float>& data) {
    // Get image data with a slicer applied
    data.resize(slicer.length().product()); // must have vector the right size before share it with Array
    casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool data_ok = _loader->GetSlice(tmp, slicer);
    ulock.unlock();
    return data_ok;
}

bool Frame::GetRegionStats(const casacore::LattRegionHolder& region, std::vector<CARTA::StatsType>& required_stats, bool per_channel,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a region applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(region, sub_image);
    ulock.unlock();
    if (subimage_ok) {
        std::lock_guard<std::mutex> guard(_image_mutex);
        return CalcStatsValues(stats_values, required_stats, sub_image, per_channel);
    }
    return subimage_ok;
}

bool Frame::GetSlicerStats(const casacore::Slicer& slicer, std::vector<CARTA::StatsType>& required_stats, bool per_channel,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a slicer applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(slicer, sub_image);
    ulock.unlock();
    if (subimage_ok) {
        std::lock_guard<std::mutex> guard(_image_mutex);
        return CalcStatsValues(stats_values, required_stats, sub_image, per_channel);
    }
    return subimage_ok;
}

bool Frame::UseLoaderSpectralData(const casacore::IPosition& region_shape) {
    // Check if loader has swizzled data and more efficient than image data
    return _loader->UseRegionSpectralData(region_shape, _image_mutex);
}

bool Frame::GetLoaderPointSpectralData(std::vector<float>& profile, int stokes, CARTA::Point& point) {
    return _loader->GetCursorSpectralData(profile, stokes, point.x(), 1, point.y(), 1, _image_mutex);
}

bool Frame::GetLoaderSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    // Get spectral data from loader (add image mutex for swizzled data)
    return _loader->GetRegionSpectralData(region_id, stokes, mask, origin, _image_mutex, results, progress);
}

bool Frame::CalculateMoments(int file_id, MomentProgressCallback progress_callback, const casacore::ImageRegion& image_region,
    const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<carta::CollapseResult>& collapse_results) {
    if (!_moment_generator) {
        _moment_generator = std::make_unique<MomentGenerator>(GetFileName(), GetImage());
    }
    if (_moment_generator) {
        std::unique_lock<std::mutex> ulock(_image_mutex); // Must lock the image while doing moment calculations
        _moment_generator->CalculateMoments(
            file_id, image_region, _spectral_axis, _stokes_axis, progress_callback, moment_request, moment_response, collapse_results);
        ulock.unlock();
    }

    return !collapse_results.empty();
}

void Frame::StopMomentCalc() {
    if (_moment_generator) {
        _moment_generator->StopCalculation();
    }
}

void Frame::SaveFile(const std::string& root_folder, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack) {
    // Input file info
    std::string in_file = GetFileName();
    casacore::ImageInterface<float>* image = GetImage();

    // Output file info
    fs::path output_filename(save_file_msg.output_file_name());
    fs::path directory(save_file_msg.output_file_directory());
    CARTA::FileType output_file_type(save_file_msg.output_file_type());

    // Set response message
    int file_id(save_file_msg.file_id());
    save_file_ack.set_file_id(file_id);
    bool success(false);
    casacore::String message;

    // Get the full resolved name of the output image
    fs::path temp_path = fs::path(root_folder) / directory;
    fs::path abs_path = fs::absolute(temp_path);
    output_filename = abs_path / output_filename;

    if (output_filename.string() == in_file) {
        message = "The source file can not be overwritten!";
        save_file_ack.set_success(success);
        save_file_ack.set_message(message);
        return;
    }

    std::unique_lock<std::mutex> ulock(_image_mutex); // Lock the image while saving the file
    if (output_file_type == CARTA::FileType::CASA) {
        // Remove the old image file if it has a same file name
        if (fs::exists(output_filename)) {
            fs::remove_all(output_filename);
        }

        // Get a copy of the original pixel data
        casacore::IPosition start(image->shape().size(), 0);
        casacore::IPosition count(image->shape());
        casacore::Slicer slice(start, count);
        casacore::Array<casacore::Float> temp_array;
        image->doGetSlice(temp_array, slice);

        // Construct a new CASA image
        try {
            auto out_image =
                std::make_unique<casacore::PagedImage<casacore::Float>>(image->shape(), image->coordinates(), output_filename.string());
            out_image->setMiscInfo(image->miscInfo());
            out_image->setImageInfo(image->imageInfo());
            out_image->appendLog(image->logger());
            out_image->setUnits(image->units());
            out_image->putSlice(temp_array, start);

            // Copy the mask if the original image has
            if (image->hasPixelMask()) {
                casacore::Array<casacore::Bool> image_mask;
                image->getMaskSlice(image_mask, slice);
                out_image->makeMask("mask0", true, true);
                casacore::Lattice<casacore::Bool>& out_image_mask = out_image->pixelMask();
                out_image_mask.putSlice(image_mask, start);
            }
        } catch (casacore::AipsError error) {
            message = error.getMesg();
            save_file_ack.set_success(false);
            save_file_ack.set_message(message);
            return;
        }
        success = true;
    } else if (output_file_type == CARTA::FileType::FITS) {
        // Remove the old image file if it has a same file name
        casacore::Bool ok = casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename.string(), 64, casacore::True,
            casacore::True, -32, 1.0, -1.0, casacore::True, casacore::False, casacore::True, casacore::False, casacore::False,
            casacore::False, casacore::String(), casacore::True);
        if (ok) {
            success = true;
        }
    } else {
        message = "No saving file action!";
    }
    ulock.unlock(); // Unlock the image

    // Remove the root folder from the ack message
    if (!root_folder.empty()) {
        std::size_t found = message.find(root_folder);
        if (found != std::string::npos) {
            message.replace(found, root_folder.size(), "");
        }
    }

    save_file_ack.set_success(success);
    save_file_ack.set_message(message);
}