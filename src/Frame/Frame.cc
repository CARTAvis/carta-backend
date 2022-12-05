/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <casacore/images/Images/SubImage.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/lattices/LRegions/LattRegionHolder.h>
#include <casacore/tables/DataMan/TiledFileAccess.h>

#include "DataStream/Compression.h"
#include "DataStream/Contouring.h"
#include "DataStream/Smoothing.h"
#include "ImageStats/StatsCalculator.h"
#include "Logger/Logger.h"
#include "Timer/Timer.h"

static const int HIGH_COMPRESSION_QUALITY(32);

namespace carta {

Frame::Frame(uint32_t session_id, std::shared_ptr<FileLoader> loader, const std::string& hdu, int default_z)
    : _session_id(session_id),
      _valid(true),
      _loader(loader),
      _tile_cache(0),
      _x_axis(0),
      _y_axis(1),
      _z_axis(-1),
      _stokes_axis(-1),
      _z_index(default_z),
      _stokes_index(DEFAULT_STOKES),
      _depth(1),
      _num_stokes(1),
      _image_cache_valid(false),
      _moment_generator(nullptr),
      _moment_name_index(0) {
    // Initialize for operator==
    _contour_settings = {std::vector<double>(), CARTA::SmoothingMode::NoSmoothing, 0, 0, 0, 0, 0};

    if (!_loader) {
        _open_image_error = fmt::format("Problem loading image: image type not supported.");
        spdlog::error("Session {}: {}", session_id, _open_image_error);
        _valid = false;
        return;
    }

    try {
        _loader->OpenFile(hdu);
    } catch (casacore::AipsError& err) {
        _open_image_error = err.getMesg();
        spdlog::error("Session {}: {}", session_id, _open_image_error);
        _valid = false;
        return;
    }

    // Get shape and axis values from the loader
    std::string log_message;
    if (!_loader->FindCoordinateAxes(_image_shape, _spectral_axis, _z_axis, _stokes_axis, log_message)) {
        _open_image_error = fmt::format("Cannot determine file shape. {}", log_message);
        spdlog::error("Session {}: {}", session_id, _open_image_error);
        _valid = false;
        return;
    }

    // Determine which axes are rendered, e.g. for pV images
    std::vector<int> render_axes = _loader->GetRenderAxes();
    _x_axis = render_axes[0];
    _y_axis = render_axes[1];

    _width = _image_shape(_x_axis);
    _height = _image_shape(_y_axis);
    _depth = (_z_axis >= 0 ? _image_shape(_z_axis) : 1);
    _num_stokes = (_stokes_axis >= 0 ? _image_shape(_stokes_axis) : 1);

    // load full image cache for loaders that don't use the tile cache and mipmaps
    if (!(_loader->UseTileCache() && _loader->HasMip(2)) && !FillImageCache()) {
        _open_image_error = fmt::format("Cannot load image data. Check log.");
        _valid = false;
        return;
    }

    // reset the tile cache if the loader will use it
    if (_loader->UseTileCache()) {
        int tiles_x = (_width - 1) / TILE_SIZE + 1;
        int tiles_y = (_height - 1) / TILE_SIZE + 1;
        int tile_cache_capacity = std::min(MAX_TILE_CACHE_CAPACITY, 2 * (tiles_x + tiles_y));
        _tile_cache.Reset(_z_index, _stokes_index, tile_cache_capacity);
    }

    // set default histogram requirements
    InitImageHistogramConfigs();
    _cube_histogram_configs.clear();

    try {
        // Resize stats vectors and load data from image, if the format supports it.
        // A failure here shouldn't invalidate the frame
        _loader->LoadImageStats();
    } catch (casacore::AipsError& err) {
        _open_image_error = fmt::format("Problem loading statistics from file: {}", err.getMesg());
        spdlog::warn("Session {}: {}", session_id, _open_image_error);
    }

    _loader->CloseImageIfUpdated();
}

bool Frame::IsValid() {
    return _valid;
}

std::string Frame::GetErrorMessage() {
    return _open_image_error;
}

std::string Frame::GetFileName() {
    std::string filename;

    if (_loader) {
        filename = _loader->GetFileName();
    }

    return filename;
}

std::shared_ptr<casacore::CoordinateSystem> Frame::CoordinateSystem(const StokesSource& stokes_source) {
    if (IsValid()) {
        return _loader->GetCoordinateSystem(stokes_source);
    }
    return std::make_shared<casacore::CoordinateSystem>();
}

casacore::IPosition Frame::ImageShape(const StokesSource& stokes_source) {
    casacore::IPosition ipos;
    if (stokes_source.IsOriginalImage() && IsValid()) {
        ipos = _image_shape;
    } else {
        auto image = _loader->GetStokesImage(stokes_source);
        if (image) {
            ipos = image->shape();
        } else {
            spdlog::error("Failed to compute the stokes image!");
        }
    }
    return ipos;
}

size_t Frame::Width() {
    return _width;
}

size_t Frame::Height() {
    return _height;
}

size_t Frame::Depth() {
    return _depth;
}

size_t Frame::NumStokes() {
    return _num_stokes;
}

int Frame::CurrentZ() {
    return _z_index;
}

int Frame::CurrentStokes() {
    return _stokes_index;
}

int Frame::SpectralAxis() {
    return _spectral_axis;
}

int Frame::StokesAxis() {
    return _stokes_axis;
}

bool Frame::GetBeams(std::vector<CARTA::Beam>& beams) {
    std::string error;
    bool beams_ok = _loader->GetBeams(beams, error);
    _loader->CloseImageIfUpdated();

    if (!beams_ok) {
        spdlog::warn("Session {}: {}", _session_id, error);
    }

    return beams_ok;
}

StokesSlicer Frame::GetImageSlicer(const AxisRange& z_range, int stokes) {
    return GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), z_range, stokes);
}

StokesSlicer Frame::GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes) {
    // Set stokes source for the image loader
    StokesSource stokes_source(stokes, z_range, x_range, y_range);

    // Slicer to apply z range and stokes to image shape
    // Start with entire image
    casacore::IPosition start(_image_shape.size());
    start = 0;
    casacore::IPosition end(_image_shape);
    end -= 1; // last position, not length

    // Slice x axis
    if (_x_axis >= 0) {
        int start_x(x_range.from), end_x(x_range.to);

        // Normalize x constants
        if (start_x == ALL_X) {
            start_x = 0;
        }
        if (end_x == ALL_X) {
            end_x = _width - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(_x_axis) = start_x;
            end(_x_axis) = end_x;
        } else { // Reset the slice cut for the computed stokes image
            start(_x_axis) = 0;
            end(_x_axis) = end_x - start_x;
        }
    }

    // Slice y axis
    if (_y_axis >= 0) {
        int start_y(y_range.from), end_y(y_range.to);

        // Normalize y constants
        if (start_y == ALL_Y) {
            start_y = 0;
        }
        if (end_y == ALL_Y) {
            end_y = _height - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(_y_axis) = start_y;
            end(_y_axis) = end_y;
        } else { // Reset the slice cut for the computed stokes image
            start(_y_axis) = 0;
            end(_y_axis) = end_y - start_y;
        }
    }

    // Slice z axis
    if (_z_axis >= 0) {
        int start_z(z_range.from), end_z(z_range.to);

        // Normalize z constants
        if (start_z == ALL_Z) {
            start_z = 0;
        } else if (start_z == CURRENT_Z) {
            start_z = CurrentZ();
        }
        if (end_z == ALL_Z) {
            end_z = Depth() - 1;
        } else if (end_z == CURRENT_Z) {
            end_z = CurrentZ();
        }

        if (stokes_source.IsOriginalImage()) {
            start(_z_axis) = start_z;
            end(_z_axis) = end_z;
        } else { // Reset the slice cut for the computed stokes image
            start(_z_axis) = 0;
            end(_z_axis) = end_z - start_z;
        }
    }

    // Slice stokes axis
    if (_stokes_axis >= 0) {
        // Normalize stokes constant
        stokes = (stokes == CURRENT_STOKES ? CurrentStokes() : stokes);

        if (stokes_source.IsOriginalImage()) {
            start(_stokes_axis) = stokes;
            end(_stokes_axis) = stokes;
        } else {
            // Reset the slice cut for the computed stokes image
            start(_stokes_axis) = 0;
            end(_stokes_axis) = 0;
        }
    }

    // slicer for image data
    casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
    return StokesSlicer(stokes_source, section);
}

bool Frame::CheckZ(int z) {
    return ((z >= 0) && (z < Depth()));
}

bool Frame::CheckStokes(int stokes) {
    return (((stokes >= 0) && (stokes < NumStokes())) || IsComputedStokes(stokes));
}

bool Frame::ZStokesChanged(int z, int stokes) {
    return (z != _z_index || stokes != _stokes_index);
}

void Frame::WaitForTaskCancellation() {
    _connected = false; // file closed
    StopMomentCalc();
    std::unique_lock lock(GetActiveTaskMutex());
}

bool Frame::IsConnected() {
    return _connected; // whether file is to be closed
}

// ********************************************************************
// Image parameters: view, z/stokes, slicers for data cache

bool Frame::SetImageChannels(int new_z, int new_stokes, std::string& message) {
    bool updated(false);

    if (!_valid) {
        message = "No file loaded";
    } else {
        if ((new_z != _z_index) || (new_stokes != _stokes_index)) {
            bool z_ok(CheckZ(new_z));
            bool stokes_ok(CheckStokes(new_stokes));
            if (z_ok && stokes_ok) {
                _z_index = new_z;
                _stokes_index = new_stokes;

                // invalidate the image cache
                InvalidateImageCache();

                if (!(_loader->UseTileCache() && _loader->HasMip(2)) || IsComputedStokes(_stokes_index)) {
                    // Reload the full channel cache for loaders which use it
                    FillImageCache();
                } else {
                    // Don't reload the full channel cache here because we may not need it

                    if (_loader->UseTileCache()) {
                        // invalidate / clear the full resolution tile cache
                        _tile_cache.Reset(_z_index, _stokes_index);
                    }
                }

                updated = true;
            } else {
                message = fmt::format("Channel {} or Stokes {} is invalid in image", new_z, new_stokes);
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
    // get image data for z, stokes

    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);

    // Exit early *after* acquiring lock if the cache has already been loaded by another thread
    if (_image_cache_valid) {
        return true;
    }

    Timer t;
    StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(_z_index), _stokes_index);
    _image_cache_size = stokes_slicer.slicer.length().product();
    _image_cache = std::make_unique<float[]>(_image_cache_size);
    if (!GetSlicerData(stokes_slicer, _image_cache.get())) {
        spdlog::error("Session {}: {}", _session_id, "Loading image cache failed.");
        return false;
    }

    auto dt = t.Elapsed();
    spdlog::performance(
        "Load {}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, dt.ms(), (float)(_width * _height) / dt.us());

    _image_cache_valid = true;
    return true;
}

void Frame::InvalidateImageCache() {
    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    _image_cache_valid = false;
}

void Frame::GetZMatrix(std::vector<float>& z_matrix, size_t z, size_t stokes) {
    // fill matrix for given z and stokes
    StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(z), stokes);
    z_matrix.resize(stokes_slicer.slicer.length().product());
    GetSlicerData(stokes_slicer, z_matrix.data());
}

// ****************************************************
// Raster Data

bool Frame::GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter) {
    // apply bounds and downsample image cache
    if (!_valid || !_image_cache_valid) {
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
    if ((_height < (y + req_height)) || (_width < (x + req_width))) {
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
    int num_image_columns = _width;
    int num_image_rows = _height;

    // read lock imageCache
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);

    Timer t;
    if (mean_filter && mip > 1) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
        BlockSmooth(
            _image_cache.get(), image_data.data(), num_image_columns, num_image_rows, row_length_region, num_rows_region, x, y, mip);
    } else {
        // Nearest neighbour filtering
        NearestNeighbor(_image_cache.get(), image_data.data(), num_image_columns, row_length_region, num_rows_region, x, y, mip);
    }

    auto dt = t.Elapsed();
    spdlog::performance("{} filter {}x{} raster data to {}x{} in {:.3f} ms at {:.3f} MPix/s",
        (mean_filter && mip > 1) ? "Mean" : "Nearest neighbour", req_height, req_width, num_rows_region, row_length_region, dt.ms(),
        (float)(num_rows_region * row_length_region) / dt.us());

    return true;
}

// Tile data
bool Frame::FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int z, int stokes,
    CARTA::CompressionType compression_type, float compression_quality) {
    // Early exit if z or stokes has changed
    if (ZStokesChanged(z, stokes)) {
        return false;
    }

    raster_tile_data.set_channel(z);
    raster_tile_data.set_stokes(stokes);
    raster_tile_data.set_compression_type(compression_type);

    if (raster_tile_data.tiles_size()) {
        raster_tile_data.clear_tiles();
    }

    CARTA::TileData* tile_ptr = raster_tile_data.add_tiles();
    tile_ptr->set_layer(tile.layer);
    tile_ptr->set_x(tile.x);
    tile_ptr->set_y(tile.y);

    std::shared_ptr<std::vector<float>> tile_data_ptr;
    int tile_width;
    int tile_height;
    if (GetRasterTileData(tile_data_ptr, tile, tile_width, tile_height)) {
        size_t tile_image_data_size = sizeof(float) * tile_data_ptr->size(); // tile image data size in bytes

        if (ZStokesChanged(z, stokes)) {
            return false;
        }
        tile_ptr->set_width(tile_width);
        tile_ptr->set_height(tile_height);
        if (compression_type == CARTA::CompressionType::NONE) {
            tile_ptr->set_image_data(tile_data_ptr->data(), sizeof(float) * tile_data_ptr->size());
            return true;
        } else if (compression_type == CARTA::CompressionType::ZFP) {
            auto nan_encodings = GetNanEncodingsBlock(*tile_data_ptr, 0, tile_width, tile_height);
            tile_ptr->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());

            if (ZStokesChanged(z, stokes)) {
                return false;
            }

            Timer t;

            // compress the data with the default precision
            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(*tile_data_ptr, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            float compression_ratio = (float)tile_image_data_size / (float)compressed_size;
            bool use_high_precision(false);

            if (precision < HIGH_COMPRESSION_QUALITY && compression_ratio > 20) {
                // re-compress the data with a higher precision
                std::vector<char> compression_buffer_hq;
                size_t compressed_size_hq;
                Compress(*tile_data_ptr, 0, compression_buffer_hq, compressed_size_hq, tile_width, tile_height, HIGH_COMPRESSION_QUALITY);
                float compression_ratio_hq = (float)tile_image_data_size / (float)compressed_size_hq;

                if (compression_ratio_hq > 10) {
                    // set compression data with high precision
                    raster_tile_data.set_compression_quality(HIGH_COMPRESSION_QUALITY);
                    tile_ptr->set_image_data(compression_buffer_hq.data(), compressed_size_hq);

                    spdlog::debug("Using high compression quality. Previous compression ratio: {:.3f}", compression_ratio);
                    compression_ratio = compression_ratio_hq;
                    use_high_precision = true;
                }
            }

            if (!use_high_precision) {
                // set compression data with default precision
                raster_tile_data.set_compression_quality(compression_quality);
                tile_ptr->set_image_data(compression_buffer.data(), compressed_size);
            }

            spdlog::debug(
                "The compression ratio for tile (layer:{}, x:{}, y:{}) is {:.3f}.", tile.layer, tile.x, tile.y, compression_ratio);

            // Measure duration for compress tile data
            auto dt = t.Elapsed();
            spdlog::performance("Compress {}x{} tile data in {:.3f} ms at {:.3f} MPix/s", tile_width, tile_height, dt.ms(),
                (float)(tile_width * tile_height) / dt.us());

            return !(ZStokesChanged(z, stokes));
        }
    }

    return false;
}

bool Frame::GetRasterTileData(std::shared_ptr<std::vector<float>>& tile_data_ptr, const Tile& tile, int& width, int& height) {
    int mip = Tile::LayerToMip(tile.layer, _width, _height, TILE_SIZE, TILE_SIZE);
    int tile_size_original = TILE_SIZE * mip;

    // crop to image size
    CARTA::ImageBounds bounds;
    bounds.set_x_min(std::max(0, tile.x * tile_size_original));
    bounds.set_x_max(std::min((int)_width, (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::max(0, tile.y * tile_size_original));
    bounds.set_y_max(std::min((int)_height, (tile.y + 1) * tile_size_original));

    const int req_height = bounds.y_max() - bounds.y_min();
    const int req_width = bounds.x_max() - bounds.x_min();
    width = std::ceil((float)req_width / mip);
    height = std::ceil((float)req_height / mip);

    std::vector<float> tile_data;
    bool loaded_data(0);

    if (mip > 1 && !IsComputedStokes(_stokes_index)) {
        // Try to load downsampled data from the image file
        loaded_data = _loader->GetDownsampledRasterData(tile_data, _z_index, _stokes_index, bounds, mip, _image_mutex);
    } else if (!_image_cache_valid && _loader->UseTileCache()) {
        // Load a tile from the tile cache only if this is supported *and* the full image cache isn't populated
        tile_data_ptr = _tile_cache.Get(TileCache::Key(bounds.x_min(), bounds.y_min()), _loader, _image_mutex);
        if (tile_data_ptr) {
            return true;
        }
    }

    // Fall back to using the full image cache.
    if (!loaded_data) {
        loaded_data = GetRasterData(tile_data, bounds, mip, true);
    }

    if (loaded_data) {
        tile_data_ptr = std::make_shared<std::vector<float>>(tile_data);
    }

    return loaded_data;
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
    // Always use the full image cache (for now)
    FillImageCache();

    double scale = 1.0;
    double offset = 0;
    bool smooth_successful = false;
    std::vector<std::vector<float>> vertex_data;
    std::vector<std::vector<int>> index_data;
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, false);

    if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::NoSmoothing || _contour_settings.smoothing_factor <= 1) {
        TraceContours(_image_cache.get(), _width, _height, scale, offset, _contour_settings.levels, vertex_data, index_data,
            _contour_settings.chunk_size, partial_contour_callback);
        return true;
    } else if (_contour_settings.smoothing_mode == CARTA::SmoothingMode::GaussianBlur) {
        // Smooth the image from cache
        int mask_size = (_contour_settings.smoothing_factor - 1) * 2 + 1;
        int64_t kernel_width = (mask_size - 1) / 2;

        int64_t source_width = _width;
        int64_t source_height = _height;
        int64_t dest_width = _width - (2 * kernel_width);
        int64_t dest_height = _height - (2 * kernel_width);
        std::unique_ptr<float[]> dest_array(new float[dest_width * dest_height]);
        smooth_successful = GaussianSmooth(
            _image_cache.get(), dest_array.get(), source_width, source_height, dest_width, dest_height, _contour_settings.smoothing_factor);
        // Can release lock early, as we're no longer using the image cache
        cache_lock.release();
        if (smooth_successful) {
            // Perform contouring with an offset based on the Gaussian smoothing apron size
            offset = _contour_settings.smoothing_factor - 1;
            TraceContours(dest_array.get(), dest_width, dest_height, scale, offset, _contour_settings.levels, vertex_data, index_data,
                _contour_settings.chunk_size, partial_contour_callback);
            return true;
        }
    } else {
        // Block averaging
        CARTA::ImageBounds image_bounds;
        image_bounds.set_x_min(0);
        image_bounds.set_y_min(0);
        image_bounds.set_x_max(_width);
        image_bounds.set_y_max(_height);

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
                _contour_settings.chunk_size, partial_contour_callback);
            return true;
        }
        spdlog::warn("Smoothing mode not implemented yet!");
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
        // set histogram requirements for histogram widgets
        HistogramConfig config;
        config.coordinate = histogram_config.coordinate();
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

bool Frame::FillRegionHistogramData(
    std::function<void(CARTA::RegionHistogramData histogram_data)> region_histogram_callback, int region_id, int file_id) {
    // fill histogram message for image plane or cube
    if ((region_id > IMAGE_REGION_ID) || (region_id < CUBE_REGION_ID)) { // does not handle other regions
        return false;
    }

    std::vector<HistogramConfig> requirements;
    if (region_id == IMAGE_REGION_ID) {
        requirements = _image_histogram_configs;
    } else {
        requirements = _cube_histogram_configs;
    }

    int stokes;
    bool have_valid_histogram(false);
    for (auto& histogram_config : requirements) {
        Timer t;

        // Set channel
        int z = histogram_config.channel;
        if ((z == CURRENT_Z) || (Depth() == 1)) {
            z = CurrentZ();
        }

        // Use number of bins in requirements
        int num_bins = histogram_config.num_bins;

        // Set stokes
        if (!GetStokesTypeIndex(histogram_config.coordinate, stokes)) {
            continue;
        }

        // create and fill region histogram data message
        auto histogram_data = Message::RegionHistogramData(file_id, region_id, z, stokes, 1.0);

        // fill histogram submessage from cache (loader or local)
        auto* histogram = histogram_data.mutable_histograms();
        bool histogram_filled = FillHistogramFromCache(z, stokes, num_bins, histogram);

        if (!histogram_filled) {
            // must calculate cube histogram from Session
            if ((region_id == CUBE_REGION_ID) || (z == ALL_Z)) {
                return false;
            }

            // calculate image histogram
            BasicStats<float> stats;
            if (GetBasicStats(z, stokes, stats)) {
                Histogram hist;
                histogram_filled = CalculateHistogram(region_id, z, stokes, num_bins, stats, hist);
                if (histogram_filled) {
                    FillHistogram(histogram, stats, hist);
                    region_histogram_callback(histogram_data); // send region histogram data message
                }
            }

            if (histogram_filled) {
                auto dt = t.Elapsed();
                spdlog::performance("Fill image histogram in {:.3f} ms at {:.3f} MPix/s", dt.ms(), (float)stats.num_pixels / dt.us());
            }
        } else {
            region_histogram_callback(histogram_data); // send region histogram data message
        }

        have_valid_histogram |= histogram_filled;
    }

    return have_valid_histogram; // true if any histograms filled
}

int Frame::AutoBinSize() {
    return int(std::max(sqrt(_width * _height), 2.0));
}

bool Frame::FillHistogramFromCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Fill Histogram submessage for given z, stokes, and num_bins
    bool filled = FillHistogramFromLoaderCache(z, stokes, num_bins, histogram);
    if (!filled) {
        filled = FillHistogramFromFrameCache(z, stokes, num_bins, histogram);
    }
    return filled;
}

bool Frame::FillHistogramFromLoaderCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Fill the Histogram submessage from the loader cache
    auto& current_stats = _loader->GetImageStats(stokes, z);
    if (current_stats.valid) {
        int image_num_bins(current_stats.histogram_bins.size());
        if ((num_bins == AUTO_BIN_SIZE) || (num_bins == image_num_bins)) {
            double min_val(current_stats.basic_stats[CARTA::StatsType::Min]);
            double max_val(current_stats.basic_stats[CARTA::StatsType::Max]);
            double mean(current_stats.basic_stats[CARTA::StatsType::Mean]);
            double std_dev(current_stats.basic_stats[CARTA::StatsType::Sigma]);

            // fill message
            double bin_width = (max_val - min_val) / image_num_bins;
            double first_bin_center = min_val + (bin_width / 2.0);
            FillHistogram(histogram, image_num_bins, bin_width, first_bin_center, current_stats.histogram_bins, mean, std_dev);
            // histogram cached in loader
            return true;
        }
    }
    return false;
}

bool Frame::FillHistogramFromFrameCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram) {
    // Get stats and histogram results from cache; also used for cube histogram
    if (num_bins == AUTO_BIN_SIZE) {
        num_bins = AutoBinSize();
    }

    bool have_histogram(false);
    Histogram hist;
    if (z == ALL_Z) {
        have_histogram = GetCachedCubeHistogram(stokes, num_bins, hist);
    } else {
        have_histogram = GetCachedImageHistogram(z, stokes, num_bins, hist);
    }

    if (have_histogram) {
        // add stats to message
        BasicStats<float> stats;
        if (GetBasicStats(z, stokes, stats)) {
            FillHistogram(histogram, stats, hist);
        }
    }
    return have_histogram;
}

bool Frame::GetBasicStats(int z, int stokes, BasicStats<float>& stats) {
    // Return basic stats from cache, or calculate (no loader option); also used for cube histogram
    if (z == ALL_Z) { // cube
        if (_cube_basic_stats.count(stokes)) {
            stats = _cube_basic_stats[stokes]; // get from cache
            return true;
        }
        return false; // calculate and cache in Session
    } else {
        int cache_key(CacheKey(z, stokes));
        if (_image_basic_stats.count(cache_key)) {
            stats = _image_basic_stats[cache_key]; // get from cache
            return true;
        }

        if ((z == CurrentZ()) && (stokes == CurrentStokes())) {
            // calculate histogram from image cache
            if ((_image_cache_size == 0) && !FillImageCache()) {
                // cannot calculate
                return false;
            }
            CalcBasicStats(stats, _image_cache.get(), _image_cache_size);
            _image_basic_stats[cache_key] = stats;
            return true;
        }

        // calculate histogram from given z/stokes data
        std::vector<float> data;
        GetZMatrix(data, z, stokes);
        CalcBasicStats(stats, data.data(), data.size());

        // cache results
        _image_basic_stats[cache_key] = stats;
        return true;
    }
    return false;
}

bool Frame::GetCachedImageHistogram(int z, int stokes, int num_bins, Histogram& hist) {
    // Get image histogram results from cache
    int cache_key(CacheKey(z, stokes));
    if (_image_histograms.count(cache_key)) {
        // get from cache if correct num_bins
        auto results_for_key = _image_histograms[cache_key];

        for (auto& result : results_for_key) {
            if (result.GetNbins() == num_bins) {
                hist = result;
                return true;
            }
        }
    }
    return false;
}

bool Frame::GetCachedCubeHistogram(int stokes, int num_bins, Histogram& hist) {
    // Get cube histogram results from cache
    if (_cube_histograms.count(stokes)) {
        for (auto& result : _cube_histograms[stokes]) {
            // get from cache if correct num_bins
            if (result.GetNbins() == num_bins) {
                hist = result;
                return true;
            }
        }
    }
    return false;
}

bool Frame::CalculateHistogram(int region_id, int z, int stokes, int num_bins, BasicStats<float>& stats, Histogram& hist) {
    // Calculate histogram for given parameters, return results
    if ((region_id > IMAGE_REGION_ID) || (region_id < CUBE_REGION_ID)) { // does not handle other regions
        return false;
    }

    if (z == ALL_Z) {
        return false; // calculation only for a specific z, even for cube histograms
    }

    if (num_bins == AUTO_BIN_SIZE) {
        num_bins = AutoBinSize();
    }

    if ((z == CurrentZ()) && (stokes == CurrentStokes())) {
        // calculate histogram from current image cache
        if ((_image_cache_size == 0) && !FillImageCache()) {
            return false;
        }
        bool write_lock(false);
        queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
        hist = CalcHistogram(num_bins, stats, _image_cache.get(), _image_cache_size);
    } else {
        // calculate histogram for z/stokes data
        std::vector<float> data;
        GetZMatrix(data, z, stokes);
        hist = CalcHistogram(num_bins, stats, data.data(), data.size());
    }

    // cache image histogram
    if ((region_id == IMAGE_REGION_ID) || (Depth() == 1)) {
        int cache_key(CacheKey(z, stokes));
        _image_histograms[cache_key].push_back(hist);
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

void Frame::CacheCubeStats(int stokes, BasicStats<float>& stats) {
    _cube_basic_stats[stokes] = stats;
}

void Frame::CacheCubeHistogram(int stokes, Histogram& hist) {
    _cube_histograms[stokes].push_back(hist);
}

// ****************************************************
// Stats Requirements and Data

bool Frame::SetStatsRequirements(int region_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& stats_configs) {
    if (region_id != IMAGE_REGION_ID) {
        return false;
    }

    _image_required_stats = stats_configs;
    return true;
}

bool Frame::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> stats_data_callback, int region_id, int file_id) {
    if (region_id != IMAGE_REGION_ID) {
        return false;
    }

    if (_image_required_stats.empty()) {
        return false; // not requested
    }

    int z(CurrentZ()); // Use current channel

    for (auto stats_config : _image_required_stats) {
        // Get stokes index
        int stokes;
        if (!GetStokesTypeIndex(stats_config.coordinate(), stokes)) {
            continue;
        }

        // Set response message
        auto stats_data = Message::RegionStatsData(file_id, region_id, z, stokes);

        // Set required stats types
        std::vector<CARTA::StatsType> required_stats;
        for (int i = 0; i < stats_config.stats_types_size(); ++i) {
            required_stats.push_back(stats_config.stats_types(i));
        }

        // Use loader image stats
        auto& image_stats = _loader->GetImageStats(stokes, z);
        if (image_stats.full) {
            FillStatistics(stats_data, required_stats, image_stats.basic_stats);
            stats_data_callback(stats_data);
            continue;
        }

        // Use cached stats
        int cache_key(CacheKey(z, stokes));
        if (_image_stats.count(cache_key)) {
            auto stats_map = _image_stats[cache_key];
            FillStatistics(stats_data, required_stats, stats_map);
            stats_data_callback(stats_data);
            continue;
        }

        Timer t;
        // Calculate stats map using slicer
        StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(z), stokes);
        bool per_z(false);
        std::map<CARTA::StatsType, std::vector<double>> stats_vector_map;
        if (GetSlicerStats(stokes_slicer, required_stats, per_z, stats_vector_map)) {
            // convert vector to single value in map
            std::map<CARTA::StatsType, double> stats_map;
            for (auto& value : stats_vector_map) {
                stats_map[value.first] = value.second[0];
            }

            // complete message
            FillStatistics(stats_data, required_stats, stats_map);
            stats_data_callback(stats_data);

            // cache results
            _image_stats[cache_key] = stats_map;

            spdlog::performance("Fill image stats in {:.3f} ms", t.Elapsed().ms());
        }
    }

    return true;
}

// ****************************************************
// Spatial Requirements and Data

void Frame::SetSpatialRequirements(const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_profiles) {
    _cursor_spatial_configs.clear();
    for (auto& profile : spatial_profiles) {
        _cursor_spatial_configs.push_back(profile);
    }
}

bool Frame::FillSpatialProfileData(std::vector<CARTA::SpatialProfileData>& spatial_data_vec) {
    return FillSpatialProfileData(_cursor, _cursor_spatial_configs, spatial_data_vec);
}

bool Frame::FillSpatialProfileData(PointXy point, std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_configs,
    std::vector<CARTA::SpatialProfileData>& spatial_data_vec) {
    // Fill spatial profile message for cursor/point region only
    // Send even if no requirements, to update value of data at cursor/point region

    // frontend does not set cursor/point region outside of image, but just in case:
    if (!point.InImage(_width, _height)) {
        return false;
    }

    Timer t;

    // The starting index of the tile which contains this index.
    // A custom tile size can be specified so that this can be reused to calculate a chunk index.
    auto tile_index = [](int index, int size = TILE_SIZE) { return (index / size) * size; };

    // The real size of the tile with this starting index, given the full size of this dimension
    auto tile_size = [](int tile_index, int total_size) { return std::min(TILE_SIZE, total_size - tile_index); };

    int x, y;
    point.ToIndex(x, y); // convert float to index into image array

    float cursor_value_with_current_stokes(0.0);

    // Get the cursor value with current stokes
    if (_image_cache_valid) {
        bool write_lock(false);
        queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
        cursor_value_with_current_stokes = _image_cache[(y * _width) + x];
        cache_lock.release();
    } else if (_loader->UseTileCache()) {
        int tile_x = tile_index(x);
        int tile_y = tile_index(y);
        auto tile = _tile_cache.Get(TileCache::Key(tile_x, tile_y), _loader, _image_mutex);
        auto tile_width = tile_size(tile_x, _width);
        cursor_value_with_current_stokes = (*tile)[((y - tile_y) * tile_width) + (x - tile_x)];
    }

    if (spatial_configs.empty()) { // Only send a spatial data message for the cursor value with current stokes
        auto spatial_data = Message::SpatialProfileData(x, y, CurrentZ(), CurrentStokes(), cursor_value_with_current_stokes);
        spatial_data_vec.push_back(spatial_data);
        return true;
    }

    // When spatial configs is not empty
    // Get point region spatial configs with respect to the stokes (key)
    std::unordered_map<int, std::vector<CARTA::SetSpatialRequirements_SpatialConfig>> point_regions_spatial_configs;

    for (auto& config : spatial_configs) {
        // Get stokes
        std::string coordinate(config.coordinate());
        int stokes;
        if (!GetStokesTypeIndex(coordinate, stokes)) {
            continue;
        }
        point_regions_spatial_configs[stokes].push_back(config);
    }

    // Get point region spatial profile data with respect to the stokes (key)
    for (auto& point_regions_spatial_config : point_regions_spatial_configs) {
        int stokes = point_regions_spatial_config.first;

        bool is_current_stokes(stokes == CurrentStokes());

        float cursor_value(0.0);

        // Get the cursor value with stokes
        if (is_current_stokes) {
            cursor_value = cursor_value_with_current_stokes;
        } else {
            StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(x), AxisRange(y), AxisRange(CurrentZ()), stokes);
            const auto N = stokes_slicer.slicer.length().product();
            std::unique_ptr<float[]> data(new float[N]); // zero initialization
            if (GetSlicerData(stokes_slicer, data.get())) {
                cursor_value = data[0];
            }
        }

        // set message fields
        auto spatial_data = Message::SpatialProfileData(x, y, CurrentZ(), stokes, cursor_value);

        // add profiles
        std::vector<float> profile;
        bool write_lock(false);

        // for each widget config with the same stokes setting
        for (auto& config : point_regions_spatial_config.second) {
            size_t start(config.start());
            size_t end(config.end());
            int mip(config.mip());

            if (!end) {
                end = config.coordinate().back() == 'x' ? _width : _height;
            }

            int requested_start(start);
            int requested_end(end);

            int decimated_start(start);
            int decimated_end(end);

            profile.clear();
            bool have_profile(false);
            bool downsample(mip >= 2);

            if (downsample && _loader->HasMip(2) && !IsComputedStokes(stokes)) { // Use a mipmap dataset to return downsampled data
                while (!_loader->HasMip(mip)) {
                    mip /= 2;
                }

                // Select the bounds of data to downsample so that it contains the requested row or column
                CARTA::ImageBounds bounds;

                if (config.coordinate().back() == 'x') {
                    bounds.set_x_min(start);
                    bounds.set_x_max(end);
                    int y_floor = std::floor((float)y / mip) * mip;
                    bounds.set_y_min(y_floor);
                    bounds.set_y_max(y_floor + mip);
                } else if (config.coordinate().back() == 'y') {
                    int x_floor = std::floor((float)x / mip) * mip;
                    bounds.set_x_min(x_floor);
                    bounds.set_x_max(x_floor + mip);
                    bounds.set_y_min(start);
                    bounds.set_y_max(end);
                }

                have_profile = _loader->GetDownsampledRasterData(profile, CurrentZ(), stokes, bounds, mip, _image_mutex);
            } else {
                if (downsample) { // Round the endpoints if we're going to decimate
                    // These values will be used to resize the decimated data
                    decimated_start = std::ceil((float)start / (mip * 2)) * 2;
                    decimated_end = std::ceil((float)end / (mip * 2)) * 2;

                    // These values will be used to fetch the data to decimate
                    start = decimated_start * mip;
                    end = decimated_end * mip;
                    end = config.coordinate().back() == 'x' ? std::min(end, _width) : std::min(end, _height);
                }

                if (is_current_stokes) {
                    if (_loader->UseTileCache()) { // Use tile cache to return full resolution data or prepare data for decimation
                        profile.resize(end - start);

                        if (config.coordinate().back() == 'x') {
                            int tile_y = tile_index(y);
                            bool ignore_interrupt(_ignore_interrupt_X_mutex.try_lock());

                            for (int tile_x = tile_index(start); tile_x <= tile_index(end - 1); tile_x += TILE_SIZE) {
                                auto key = TileCache::Key(tile_x, tile_y);
                                // The cursor/point region has moved outside this chunk row
                                if (!ignore_interrupt && (tile_index(point.y, CHUNK_SIZE) != TileCache::ChunkKey(key).y)) {
                                    return have_profile;
                                }
                                auto tile = _tile_cache.Get(key, _loader, _image_mutex);
                                auto tile_width = tile_size(tile_x, _width);
                                auto tile_height = tile_size(tile_y, _height);

                                // copy contiguous row
                                auto y_offset = tile->begin() + tile_width * (y - tile_y);
                                auto tile_start = y_offset + max(start - tile_x, 0);
                                auto tile_end = y_offset + min(end - tile_x, tile_width);
                                auto profile_start = profile.begin() + max(tile_x - start, 0);
                                std::copy(tile_start, tile_end, profile_start);
                            }

                            have_profile = true;

                        } else if (config.coordinate().back() == 'y') {
                            int tile_x = tile_index(x);
                            bool ignore_interrupt(_ignore_interrupt_Y_mutex.try_lock());

                            for (int tile_y = tile_index(start); tile_y <= tile_index(end - 1); tile_y += TILE_SIZE) {
                                auto key = TileCache::Key(tile_x, tile_y);
                                // The point region has moved outside this chunk column
                                if (!ignore_interrupt && (tile_index(point.x, CHUNK_SIZE) != TileCache::ChunkKey(key).x)) {
                                    return have_profile;
                                }
                                auto tile = _tile_cache.Get(key, _loader, _image_mutex);
                                auto tile_width = tile_size(tile_x, _width);
                                auto tile_height = tile_size(tile_y, _height);

                                // copy non-contiguous column

                                auto tile_start = max(start - tile_y, 0);
                                auto tile_end = min(end - tile_y, tile_height);
                                auto profile_start = max(tile_y - start, 0);

                                for (int j = tile_start; j < tile_end; j++) {
                                    profile[profile_start + j - tile_start] = (*tile)[(j * tile_width) + (x - tile_x)];
                                }
                            }
                            have_profile = true;
                        }
                    } else { // Use image cache to return full resolution data or prepare data for decimation
                        profile.reserve(end - start);

                        if (config.coordinate().back() == 'x') {
                            auto x_start = y * _width;
                            queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
                            for (unsigned int j = start; j < end; ++j) {
                                auto idx = x_start + j;
                                profile.push_back(_image_cache[idx]);
                            }
                            cache_lock.release();
                        } else if (config.coordinate().back() == 'y') {
                            queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
                            for (unsigned int j = start; j < end; ++j) {
                                auto idx = (j * _width) + x;
                                profile.push_back(_image_cache[idx]);
                            }
                            cache_lock.release();
                        }

                        have_profile = true;
                    }
                } else { // When required stokes is not the current stokes or the stokes needs to be computed
                    profile.reserve(end - start);

                    StokesSlicer stokes_slicer;
                    if (config.coordinate().back() == 'x') {
                        stokes_slicer = GetImageSlicer(AxisRange(start, end - 1), AxisRange(y), AxisRange(CurrentZ()), stokes);
                    } else if (config.coordinate().back() == 'y') {
                        stokes_slicer = GetImageSlicer(AxisRange(x), AxisRange(start, end - 1), AxisRange(CurrentZ()), stokes);
                    }

                    profile.resize(stokes_slicer.slicer.length().product());
                    have_profile = GetSlicerData(stokes_slicer, profile.data());
                }
            }

            // decimate the profile in-place, attempting to preserve order
            if (have_profile && downsample && !_loader->HasMip(2)) {
                for (size_t i = 0; i < profile.size(); i += mip * 2) {
                    float min_pix = std::numeric_limits<float>::max();
                    float max_pix = std::numeric_limits<float>::lowest();
                    int min_pos(-1), max_pos(-1), idx(0);

                    auto get_minmax = [&](const float& value) {
                        if (!std::isnan(value)) {
                            if (value < min_pix) {
                                min_pix = value;
                                min_pos = idx;
                            }
                            if (value > max_pix) {
                                max_pix = value;
                                max_pos = idx;
                            }
                        }
                        ++idx;
                    };

                    std::for_each(profile.begin() + i, std::min(profile.begin() + i + mip * 2, profile.end()), get_minmax);

                    if (min_pos > -1 && max_pos > -1) {
                        if (min_pos < max_pos) {
                            profile[i / mip] = min_pix;
                            profile[i / mip + 1] = max_pix;
                        } else {
                            profile[i / mip] = max_pix;
                            profile[i / mip + 1] = min_pix;
                        }
                    } else if (min_pos > -1) {
                        profile[i / mip] = profile[i / mip + 1] = min_pix;
                    } else if (max_pos > -1) {
                        profile[i / mip] = profile[i / mip + 1] = max_pix;
                    } else {
                        profile[i / mip] = profile[i / mip + 1] = std::numeric_limits<float>::quiet_NaN();
                    }
                }
                profile.resize(decimated_end - decimated_start); // shrink the profile to the downsampled size
            }

            if (have_profile) {
                // add SpatialProfile to message
                auto spatial_profile = spatial_data.add_profiles();
                spatial_profile->set_coordinate(config.coordinate());
                // Should these be set to the rounded endpoints if the data is downsampled or decimated?
                spatial_profile->set_start(requested_start);
                spatial_profile->set_end(requested_end);
                spatial_profile->set_raw_values_fp32(profile.data(), profile.size() * sizeof(float));
                spatial_profile->set_mip(mip);
            }
        }

        // Fill the spatial profile data with respect to the stokes in a vector
        spatial_data_vec.emplace_back(spatial_data);
    }

    spdlog::performance("Fill spatial profile in {:.3f} ms", t.Elapsed().ms());

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
        int stokes;
        if (!GetStokesTypeIndex(coordinate, stokes)) {
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

    // No z axis
    if (_z_axis < 0) {
        return false;
    }

    // No spectral profile requirements
    if (_cursor_spectral_configs.empty()) {
        return false;
    }

    std::shared_lock lock(GetActiveTaskMutex());

    PointXy start_cursor = _cursor; // if cursor changes, cancel profiles

    Timer t;
    std::vector<SpectralConfig> current_configs;
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    current_configs.insert(current_configs.begin(), _cursor_spectral_configs.begin(), _cursor_spectral_configs.end());
    ulock.unlock();

    for (auto& config : current_configs) {
        if (!(_cursor == start_cursor) || !IsConnected()) {
            // cursor changed or file closed, cancel profiles
            return false;
        }
        if (!HasSpectralConfig(config)) {
            // requirements changed
            return false;
        }

        std::string coordinate(config.coordinate);
        if ((coordinate != "z") && stokes_changed) {
            continue; // do not send fixed stokes profile when stokes changes
        }

        // Create final profile message for callback
        auto profile_message = Message::SpectralProfileData(CurrentStokes(), 1.0);
        auto spectral_profile = profile_message.add_profiles();
        spectral_profile->set_coordinate(config.coordinate);
        // point spectral profiles only have one stats type
        spectral_profile->set_stats_type(config.all_stats[0]);

        // Send spectral profile data if cursor inside image
        if (start_cursor.InImage(_width, _height)) {
            int stokes;
            if (!GetStokesTypeIndex(coordinate, stokes)) {
                continue;
            }

            std::vector<float> spectral_data;
            int xy_count(1);
            if (!IsComputedStokes(stokes) && _loader->GetCursorSpectralData(spectral_data, stokes, (start_cursor.x + 0.5), xy_count,
                                                 (start_cursor.y + 0.5), xy_count, _image_mutex)) {
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
                start(_z_axis) = 0;
                if (_stokes_axis >= 0) {
                    start(_stokes_axis) = stokes;
                }
                casacore::IPosition count(_image_shape.size(), 1); // will adjust count for z axis
                size_t end_channel(0);

                // Send incremental spectral profile when reach delta z or delta time
                size_t delta_z = INIT_DELTA_Z;                         // the increment of channels for each slice (to be adjusted)
                size_t dt_slice_target = TARGET_DELTA_TIME;            // target time elapse for each slice, in milliseconds
                size_t dt_partial_update = TARGET_PARTIAL_CURSOR_TIME; // time increment to send an update
                size_t profile_size = Depth();                         // profile vector size
                spectral_data.resize(profile_size, NAN);
                float progress(0.0);

                auto t_start_profile = std::chrono::high_resolution_clock::now();

                while (progress < 1.0) {
                    // start timer for slice
                    auto t_start_slice = std::chrono::high_resolution_clock::now();

                    // Slice image to get next delta_z (not to exceed depth in image)
                    size_t nz = (start(_z_axis) + delta_z < profile_size ? delta_z : profile_size - start(_z_axis));
                    count(_z_axis) = nz;
                    casacore::Slicer slicer(start, count);
                    const auto N = slicer.length().product();
                    std::unique_ptr<float[]> buffer(new float[N]);
                    end_channel = start(_z_axis) + nz - 1;
                    auto stokes_slicer =
                        GetImageSlicer(AxisRange(x_index), AxisRange(y_index), AxisRange(start(_z_axis), end_channel), stokes);
                    if (!GetSlicerData(stokes_slicer, buffer.get())) {
                        return false;
                    }
                    // copy buffer to spectral_data
                    memcpy(&spectral_data[start(_z_axis)], buffer.get(), nz * sizeof(float));
                    // update start z and determine progress
                    start(_z_axis) += nz;
                    progress = (float)start(_z_axis) / profile_size;

                    // get the time elapse for this slice
                    auto t_end_slice = std::chrono::high_resolution_clock::now();
                    auto dt_slice = std::chrono::duration<double, std::milli>(t_end_slice - t_start_slice).count();
                    auto dt_profile = std::chrono::duration<double, std::milli>(t_end_slice - t_start_profile).count();

                    // adjust delta z per slice according to the time elapse,
                    // to achieve target elapsed time per slice TARGET_DELTA_TIME (used to check for cancel)
                    if (delta_z == INIT_DELTA_Z) {
                        delta_z *= dt_slice_target / dt_slice;
                        if (delta_z < 1) {
                            delta_z = 1;
                        }
                        if (delta_z > profile_size) {
                            delta_z = profile_size;
                        }
                    }

                    // Check for cancel before sending
                    if (!(_cursor == start_cursor) || !IsConnected()) { // cursor changed or file closed, cancel all profiles
                        return false;
                    }
                    if (!HasSpectralConfig(config)) {
                        // requirements changed, cancel this profile
                        break;
                    }

                    if (progress >= 1.0) {
                        spectral_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                        // send final profile message
                        cb(profile_message);
                    } else if (dt_profile > dt_partial_update) {
                        // reset profile timer and send partial profile message
                        t_start_profile = t_end_slice;

                        auto partial_data = Message::SpectralProfileData(CurrentStokes(), progress);
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

    spdlog::performance("Fill cursor spectral profile in {:.3f} ms", t.Elapsed().ms());

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

std::shared_ptr<casacore::LCRegion> Frame::GetImageRegion(
    int file_id, std::shared_ptr<Region> region, const StokesSource& stokes_source, bool report_error) {
    // Return LCRegion formed by applying region params to image.
    // Returns nullptr if region outside image
    return region->GetImageRegion(file_id, CoordinateSystem(stokes_source), ImageShape(stokes_source), stokes_source, report_error);
}

bool Frame::GetImageRegion(int file_id, const AxisRange& z_range, int stokes, StokesRegion& stokes_region) {
    if (!CheckZ(z_range.from) || !CheckZ(z_range.to) || !CheckStokes(stokes)) {
        return false;
    }
    try {
        StokesSlicer stokes_slicer = GetImageSlicer(z_range, stokes);
        stokes_region.stokes_source = stokes_slicer.stokes_source;
        casacore::LCSlicer lcslicer(stokes_slicer.slicer);
        casacore::ImageRegion this_region(lcslicer);
        stokes_region.image_region = this_region;
        return true;
    } catch (casacore::AipsError error) {
        spdlog::error("Error converting full region to file {}: {}", file_id, error.getMesg());
        return false;
    }
}

casacore::IPosition Frame::GetRegionShape(const StokesRegion& stokes_region) {
    // Returns image shape with a region applied
    auto coord_sys = CoordinateSystem(stokes_region.stokes_source);
    casacore::LatticeRegion lattice_region =
        stokes_region.image_region.toLatticeRegion(*coord_sys.get(), ImageShape(stokes_region.stokes_source));
    return lattice_region.shape();
}

bool Frame::GetRegionData(const StokesRegion& stokes_region, std::vector<float>& data) {
    // Get image data with a region applied
    Timer t;
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(stokes_region, sub_image);
    ulock.unlock();

    if (!subimage_ok) {
        return false;
    }

    casacore::IPosition subimage_shape = sub_image.shape();
    if (subimage_shape.empty()) {
        return false;
    }

    try {
        casacore::IPosition start(subimage_shape.size(), 0);
        casacore::IPosition count(subimage_shape);
        casacore::Slicer slicer(start, count); // entire subimage
        bool is_computed_stokes(!stokes_region.stokes_source.IsOriginalImage());

        // Get image data
        std::unique_lock<std::mutex> ulock(_image_mutex);
        if (_loader->IsGenerated() || is_computed_stokes) { // For the image in memory
            casacore::Array<float> tmp;
            sub_image.doGetSlice(tmp, slicer);
            data = tmp.tovector();
        } else {
            data.resize(subimage_shape.product()); // must size correctly before sharing
            casacore::Array<float> tmp(subimage_shape, data.data(), casacore::StorageInitPolicy::SHARE);
            sub_image.doGetSlice(tmp, slicer);
        }

        // Get mask that defines region in subimage bounding box
        casacore::Array<bool> tmpmask;
        sub_image.doGetMaskSlice(tmpmask, slicer);
        ulock.unlock();

        // Apply mask to data
        std::vector<bool> datamask = tmpmask.tovector();
        for (size_t i = 0; i < data.size(); ++i) {
            if (!datamask[i]) {
                data[i] = NAN;
            }
        }

        spdlog::performance("Get region subimage data in {:.3f} ms", t.Elapsed().ms());

        return true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }

    return false;
}

bool Frame::GetSlicerData(const StokesSlicer& stokes_slicer, float* data) {
    // Get image data with a slicer applied
    casacore::Array<float> tmp(stokes_slicer.slicer.length(), data, casacore::StorageInitPolicy::SHARE);
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool data_ok = _loader->GetSlice(tmp, stokes_slicer);
    _loader->CloseImageIfUpdated();
    ulock.unlock();
    return data_ok;
}

bool Frame::GetRegionStats(const StokesRegion& stokes_region, const std::vector<CARTA::StatsType>& required_stats, bool per_z,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a region applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(stokes_region, sub_image);
    _loader->CloseImageIfUpdated();
    ulock.unlock();

    if (subimage_ok) {
        std::lock_guard<std::mutex> guard(_image_mutex);
        return CalcStatsValues(stats_values, required_stats, sub_image, per_z);
    }

    return subimage_ok;
}

bool Frame::GetSlicerStats(const StokesSlicer& stokes_slicer, std::vector<CARTA::StatsType>& required_stats, bool per_z,
    std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Get stats for image data with a slicer applied
    casacore::SubImage<float> sub_image;
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool subimage_ok = _loader->GetSubImage(stokes_slicer, sub_image);
    _loader->CloseImageIfUpdated();
    ulock.unlock();

    if (subimage_ok) {
        std::lock_guard<std::mutex> guard(_image_mutex);
        return CalcStatsValues(stats_values, required_stats, sub_image, per_z);
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

bool Frame::GetLoaderSpectralData(int region_id, const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    // Get spectral data from loader (add image mutex for swizzled data)
    return _loader->GetRegionSpectralData(region_id, z_range, stokes, mask, origin, _image_mutex, results, progress);
}

bool Frame::CalculateMoments(int file_id, GeneratorProgressCallback progress_callback, const StokesRegion& stokes_region,
    const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results,
    RegionState region_state) {
    std::shared_lock lock(GetActiveTaskMutex());
    _moment_generator.reset(new MomentGenerator(GetFileName(), _loader->GetStokesImage(stokes_region.stokes_source)));
    _loader->CloseImageIfUpdated();

    if (region_state.control_points.empty()) {
        region_state.type = CARTA::RegionType::RECTANGLE;
        region_state.control_points = {Message::Point(0, 0), Message::Point(_width - 1, _height - 1)};
        region_state.rotation = 0.0;
    }

    if (_moment_generator) {
        int name_index(0);
        if (moment_request.keep()) {
            name_index = ++_moment_name_index;
        }

        std::unique_lock<std::mutex> ulock(_image_mutex); // Must lock the image while doing moment calculations
        _moment_generator->CalculateMoments(file_id, stokes_region.image_region, _z_axis, _stokes_axis, name_index, progress_callback,
            moment_request, moment_response, collapse_results, region_state, GetStokesType(CurrentStokes()));
        ulock.unlock();
    }

    return !collapse_results.empty();
}

void Frame::StopMomentCalc() {
    if (_moment_generator) {
        _moment_generator->StopCalculation();
    }
}

bool Frame::FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response, GeneratedImage& model_image,
    GeneratedImage& residual_image, GeneratorProgressCallback progress_callback, StokesRegion* stokes_region) {
    if (!_image_fitter) {
        _image_fitter = std::make_unique<ImageFitter>();
    }

    bool success = false;

    if (_image_fitter) {
        std::vector<CARTA::GaussianComponent> initial_values(
            fitting_request.initial_values().begin(), fitting_request.initial_values().end());
        std::vector<bool> fixed_params(fitting_request.fixed_params().begin(), fitting_request.fixed_params().end());

        if (stokes_region != nullptr) {
            casacore::IPosition region_shape = GetRegionShape(*stokes_region);
            spdlog::info("Creating region subimage data with shape {} x {}.", region_shape(0), region_shape(1));

            std::vector<float> region_data;
            if (!GetRegionData(*stokes_region, region_data)) {
                spdlog::error("Failed to get data in the region!");
                fitting_response.set_message("failed to get data");
                fitting_response.set_success(false);
                return false;
            }

            casacore::IPosition origin(2, 0, 0);
            casacore::IPosition region_origin = stokes_region->image_region.asLCRegion().expand(origin);

            success = _image_fitter->FitImage(region_shape(0), region_shape(1), region_data.data(), initial_values, fixed_params,
                fitting_request.create_model_image(), fitting_request.create_residual_image(), fitting_response, progress_callback,
                region_origin(0), region_origin(1));
        } else {
            FillImageCache();
            success = _image_fitter->FitImage(_width, _height, _image_cache.get(), initial_values, fixed_params,
                fitting_request.create_model_image(), fitting_request.create_residual_image(), fitting_response, progress_callback);
        }

        if (success && (fitting_request.create_model_image() || fitting_request.create_residual_image())) {
            int file_id(fitting_request.file_id());
            StokesRegion output_stokes_region;
            if (stokes_region != nullptr) {
                output_stokes_region = *stokes_region;
            } else {
                GetImageRegion(file_id, AxisRange(CurrentZ()), CurrentStokes(), output_stokes_region);
            }
            casa::SPIIF image(_loader->GetStokesImage(output_stokes_region.stokes_source));
            success = _image_fitter->GetGeneratedImages(
                image, output_stokes_region.image_region, file_id, GetFileName(), model_image, residual_image, fitting_response);
        }
    }

    return success;
}

void Frame::StopFitting() {
    spdlog::debug("Cancelling image fitting.");
    if (_image_fitter) {
        _image_fitter->StopFitting();
    }
}

// Export modified image to file, for changed range of channels/stokes and chopped region
// Input root_folder as target path
// Input save_file_msg as requesting parameters
// Input save_file_ack as responding message to frontend
// Input region as std::shared_ptr<Region> of information of target region
// Return void
void Frame::SaveFile(const std::string& root_folder, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack,
    std::shared_ptr<Region> region) {
    // Input file info
    std::string in_file = GetFileName();

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

    double rest_freq(save_file_msg.rest_freq());
    bool change_rest_freq = !std::isnan(rest_freq);

    // Try to save file from loader (for entire LEL image in CASA format only)
    if (!region && !change_rest_freq) {
        if (_loader->SaveFile(output_file_type, output_filename.string(), message)) {
            save_file_ack.set_success(true);
            return;
        }
    }

    // Begin with entire image
    auto image_shape = ImageShape();
    casacore::ImageInterface<float>* image = _loader->GetImage().get();

    // Modify image to export
    casacore::SubImage<float> sub_image;
    std::shared_ptr<casacore::LCRegion> image_region;
    casacore::IPosition region_shape;

    if (region) {
        image_region = GetImageRegion(file_id, region);

        if (!image_region) {
            save_file_ack.set_success(false);
            save_file_ack.set_message("The selected region is entirely outside the image.");
            return;
        }

        region_shape = image_region->shape();
    }

    //// Todo: support saving computed stokes images
    if (image_shape.size() == 2) {
        if (region) {
            _loader->GetSubImage(StokesRegion(StokesSource(), ImageRegion(image_region->cloneRegion())), sub_image);
            image = sub_image.cloneII();
            _loader->CloseImageIfUpdated();
        }
    } else if (image_shape.size() > 2 && image_shape.size() < 5) {
        try {
            if (region) {
                auto latt_region_holder = LattRegionHolder(image_region->cloneRegion());
                auto slice_sub_image = GetExportRegionSlicer(save_file_msg, image_shape, region_shape, latt_region_holder);

                _loader->GetSubImage(slice_sub_image, latt_region_holder, sub_image);
            } else {
                auto slice_sub_image = GetExportImageSlicer(save_file_msg, image_shape);
                _loader->GetSubImage(StokesSlicer(StokesSource(), slice_sub_image), sub_image);
            }

            // If keep degenerated axes
            if (save_file_msg.keep_degenerate()) {
                image = sub_image.cloneII();
            } else {
                image = casacore::SubImage<float>(sub_image, casacore::AxesSpecifier(false), true).cloneII();
            }
        } catch (casacore::AipsError error) {
            message = error.getMesg();
            save_file_ack.set_success(false);
            save_file_ack.set_message(message);
            return;
        }
    } else {
        return;
    }

    if (change_rest_freq) {
        casacore::CoordinateSystem coord_sys = image->coordinates();
        casacore::String error_msg("");
        bool success = coord_sys.setRestFrequency(error_msg, casacore::Quantity(rest_freq, casacore::Unit("Hz")));
        if (success) {
            success = image->setCoordinateInfo(coord_sys);
        }
        if (!success) {
            spdlog::warn("Failed to set new rest freq; use header rest freq instead: {}", error_msg);
        }
    }

    // Export image data to file
    try {
        std::unique_lock<std::mutex> ulock(_image_mutex); // Lock the image while saving the file
        {
            switch (output_file_type) {
                case CARTA::FileType::CASA:
                    success = ExportCASAImage(*image, output_filename, message);
                    break;
                case CARTA::FileType::FITS:
                    success = ExportFITSImage(*image, output_filename, message);
                    break;
                default:
                    message = fmt::format("Could not export file. Unknown file type {}.", FileTypeString[output_file_type]);
                    break;
            }
        }
        ulock.unlock(); // Unlock the image
    } catch (casacore::AipsError error) {
        message += error.getMesg();
        save_file_ack.set_success(false);
        save_file_ack.set_message(message);
        return;
    }
    if (success) {
        spdlog::info("Exported a {} file \'{}\'.", FileTypeString[output_file_type], output_filename.string());
    }

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

// Export FileLoader::ImageRef image to CASA file
// Input casacore::ImageInterface<casacore::Float> image as source data
// Input output_filename as file path
// Input message as a return message, which may contain error message
// Return a bool if this functionality success
bool Frame::ExportCASAImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message) {
    bool success(false);

    // Remove the old image file if it has a same file name
    std::error_code error_code;
    if (fs::exists(output_filename, error_code)) {
        fs::remove_all(output_filename);
    }

    // Get a copy of all pixel data
    casacore::IPosition start(image.shape().size(), 0);
    casacore::IPosition count(image.shape());
    casacore::Slicer slice(start, count);
    casacore::Array<casacore::Float> temp_array;
    image.doGetSlice(temp_array, slice);

    // Construct a new CASA image
    try {
        auto out_image =
            std::make_unique<casacore::PagedImage<casacore::Float>>(image.shape(), image.coordinates(), output_filename.string());
        out_image->setMiscInfo(image.miscInfo());
        out_image->setImageInfo(image.imageInfo());
        out_image->appendLog(image.logger());
        out_image->setUnits(image.units());
        out_image->putSlice(temp_array, start);

        // Create the mask for region
        if (image.hasPixelMask()) {
            casacore::Array<casacore::Bool> image_mask;
            image.getMaskSlice(image_mask, slice);
            out_image->makeMask("mask0", true, true);
            casacore::Lattice<casacore::Bool>& out_image_mask = out_image->pixelMask();
            out_image_mask.putSlice(image_mask, start);
        }
        success = true;
    } catch (casacore::AipsError error) {
        message = error.getMesg();
    }

    return success;
}

// Export FileLoader::ImageRef image to FITS file
// Input casacore::ImageInterface<casacore::Float> image as source data
// Input output_filename as file path
// Input message as a return message, which may contain error message
// Return a bool if this functionality success
bool Frame::ExportFITSImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message) {
    bool success = false;
    bool prefer_velocity;
    bool optical_velocity;
    bool prefer_wavelength;
    bool air_wavelength;
    GetSpectralCoordPreferences(&image, prefer_velocity, optical_velocity, prefer_wavelength, air_wavelength);

    casacore::String error_string;
    casacore::String origin_string;
    bool allow_overwrite(true);
    bool degenerate_last(false);
    bool verbose(true);
    bool stokes_last(false);
    bool history(true);
    const int bit_pix(-32);
    const float min_pix(1.0);
    const float max_pix(-1.0);
    if (casacore::ImageFITSConverter::ImageToFITS(error_string, image, output_filename.string(), 64, prefer_velocity, optical_velocity,
            bit_pix, min_pix, max_pix, allow_overwrite, degenerate_last, verbose, stokes_last, prefer_wavelength, air_wavelength,
            origin_string, history)) {
        success = true;
    } else {
        message = error_string;
    }
    return success;
}

// Validate channels & stokes, if it starts from 0 and ends in the maximum range
// Output channels as [channels_start, channels_end, channels_stride], default [0, channels_number, 1]
// Output stokes as [stokes_start, stokes_end, stokes_stride], default [0, stokes_number, 1]
// Input save_file_msg as the parameters from the request, which gives input range
// Return void
void Frame::ValidateChannelStokes(std::vector<int>& channels, std::vector<int>& stokes, const CARTA::SaveFile& save_file_msg) {
    auto image_shape = ImageShape();

    // Default for channels
    int channels_max = _z_axis > -1 ? image_shape[_z_axis] : 1;
    int channels_start = 0;
    int channels_stride = 1;
    int channels_end = channels_max - 1;
    int channels_length = channels_max;
    // Clamp channels range
    if (save_file_msg.channels().size() > 0) {
        channels_start = std::min(std::max(save_file_msg.channels(0), 0), channels_max - 1);
        channels_end = std::min(std::max(save_file_msg.channels(1), channels_start), channels_max - 1);
    }

    // Default for stokes
    int stokes_max = _stokes_axis > -1 ? image_shape[_stokes_axis] : 1;
    int stokes_start = 0;
    int stokes_stride = 1;
    int stokes_end = stokes_max - 1;
    int stokes_length = stokes_max;
    // Clamp stokes range
    if (save_file_msg.stokes().size() > 0) {
        stokes_start = std::min(std::max(save_file_msg.stokes(0), 0), stokes_max - 1);
        stokes_end = std::min(std::max(save_file_msg.stokes(1), stokes_start), stokes_max - 1);
        stokes_stride = std::round(std::min(std::max(save_file_msg.stokes(2), 1), stokes_max - stokes_start));
    }

    // Ouput results
    channels.push_back(channels_start);
    channels.push_back(channels_end);
    channels.push_back(channels_stride);
    stokes.push_back(stokes_start);
    stokes.push_back(stokes_end);
    stokes.push_back(stokes_stride);
}

// Calculate Slicer for a given image with modified channels/stokes
// Input save_file_msg as the parameters from the request, which gives input range
// Input image_shape as casacore::IPosition of source image
// Return casacore::Slicer(start, end, stride) for apply subImage()
casacore::Slicer Frame::GetExportImageSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape) {
    auto channels = std::vector<int>();
    auto stokes = std::vector<int>();
    ValidateChannelStokes(channels, stokes, save_file_msg);

    casacore::IPosition start;
    casacore::IPosition end;
    casacore::IPosition stride;
    switch (image_shape.size()) {
        // 3 dimensional cube image
        case 3:
            if (_z_axis == 2) {
                // Channels present
                start = casacore::IPosition(3, 0, 0, channels[0]);
                end = casacore::IPosition(3, image_shape[0] - 1, image_shape[1] - 1, channels[1]);
                stride = casacore::IPosition(3, 1, 1, channels[2]);
            } else {
                // Stokes present
                start = casacore::IPosition(3, 0, 0, stokes[0]);
                end = casacore::IPosition(3, image_shape[0] - 1, image_shape[1] - 1, stokes[1]);
                stride = casacore::IPosition(3, 1, 1, stokes[2]);
            }
            break;
        // 4 dimensional cube image
        case 4:
            if (_z_axis == 2) {
                // Channels present before stokes
                start = casacore::IPosition(4, 0, 0, channels[0], stokes[0]);
                end = casacore::IPosition(4, image_shape[0] - 1, image_shape[1] - 1, channels[1], stokes[1]);
                stride = casacore::IPosition(4, 1, 1, channels[2], stokes[2]);
            } else {
                // Channels present after stokes
                start = casacore::IPosition(4, 0, 0, stokes[0], channels[0]);
                end = casacore::IPosition(4, image_shape[0] - 1, image_shape[1] - 1, stokes[1], channels[1]);
                stride = casacore::IPosition(4, 1, 1, stokes[2], channels[2]);
            }
            break;
        default:
            break;
    }
    return casacore::Slicer(start, end, stride, casacore::Slicer::endIsLast);
}

// Calculate Slicer/LattRegionHolder for a given region with modified channels/stokes
// Input save_file_msg as the parameters from the request, which gives input range
// Input image_shape as casacore::IPosition of source image
// Input region_shape as casacore::IPosition of target region
// Input image_region as casacore::LCRegion of infomation of target region to chop
// Input latt_region_holder as casacore::LattRegionHolder of target LCRegion (or will throw exception)
// Output latt_region_holder modified if dimension of region does not match the source image
// Return casacore::Slicer(start, end, stride) for apply subImage()
casacore::Slicer Frame::GetExportRegionSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape,
    casacore::IPosition region_shape, casacore::LattRegionHolder& latt_region_holder) {
    auto channels = std::vector<int>();
    auto stokes = std::vector<int>();
    ValidateChannelStokes(channels, stokes, save_file_msg);

    casacore::IPosition start;
    casacore::IPosition end;
    casacore::IPosition stride;

    switch (image_shape.size()) {
        // 3 dimensional cube image
        case 3:
            if (_z_axis == 2) {
                // Channels present
                start = casacore::IPosition(3, 0, 0, channels[0]);
                end = casacore::IPosition(3, region_shape[0] - 1, region_shape[1] - 1, channels[1]);
                stride = casacore::IPosition(3, 1, 1, channels[2]);
                if (region_shape.size() < image_shape.size()) {
                    auto region_ext = casacore::LCExtension(*latt_region_holder.asLCRegionPtr(), casacore::IPosition(1, 2),
                        casacore::LCBox(
                            casacore::IPosition(1, 0), casacore::IPosition(1, image_shape[2]), casacore::IPosition(1, image_shape[2])));
                    latt_region_holder = LattRegionHolder(region_ext);
                }
            } else {
                // Stokes present
                start = casacore::IPosition(3, 0, 0, stokes[0]);
                end = casacore::IPosition(3, region_shape[0] - 1, region_shape[1] - 1, stokes[1]);
                stride = casacore::IPosition(3, 1, 1, stokes[2]);
                if (region_shape.size() < image_shape.size()) {
                    auto region_ext = casacore::LCExtension(*latt_region_holder.asLCRegionPtr(), casacore::IPosition(1, 2),
                        casacore::LCBox(
                            casacore::IPosition(1, 0), casacore::IPosition(1, image_shape[3]), casacore::IPosition(1, image_shape[3])));
                    latt_region_holder = LattRegionHolder(region_ext);
                }
            }
            break;
        // 4 dimensional cube image
        case 4:
            if (_z_axis == 2) {
                // Channels present before stokes
                start = casacore::IPosition(4, 0, 0, channels[0], stokes[0]);
                end = casacore::IPosition(4, region_shape[0] - 1, region_shape[1] - 1, channels[1], stokes[1]);
                stride = casacore::IPosition(4, 1, 1, channels[2], stokes[2]);
                if (region_shape.size() < image_shape.size()) {
                    auto region_ext = casacore::LCExtension(*latt_region_holder.asLCRegionPtr(), casacore::IPosition(2, 2, 3),
                        casacore::LCBox(casacore::IPosition(2, 0, 0), casacore::IPosition(2, image_shape[2], image_shape[3]),
                            casacore::IPosition(2, image_shape[2], image_shape[3])));
                    latt_region_holder = LattRegionHolder(region_ext);
                }
            } else {
                // Channels present after stokes
                start = casacore::IPosition(4, 0, 0, stokes[0], channels[0]);
                end = casacore::IPosition(4, region_shape[0] - 1, region_shape[1] - 1, stokes[1], channels[1]);
                stride = casacore::IPosition(4, 1, 1, stokes[2], channels[2]);
                if (region_shape.size() < image_shape.size()) {
                    auto region_ext = casacore::LCExtension(*latt_region_holder.asLCRegionPtr(), casacore::IPosition(2, 2, 3),
                        casacore::LCBox(casacore::IPosition(2, 0, 0), casacore::IPosition(2, image_shape[3], image_shape[2]),
                            casacore::IPosition(2, image_shape[3], image_shape[2])));
                    latt_region_holder = LattRegionHolder(region_ext);
                }
            }
            break;
        default:
            break;
    }
    return casacore::Slicer(start, end, stride, casacore::Slicer::endIsLast);
}

bool Frame::GetStokesTypeIndex(const string& coordinate, int& stokes_index) {
    // Coordinate could be profile (x, y, z), stokes string (I, Q, U), or combination (Ix, Qy)
    bool is_stokes_string = StokesStringTypes.find(coordinate) != StokesStringTypes.end();
    bool is_combination = (coordinate.size() > 1 && (coordinate.back() == 'x' || coordinate.back() == 'y' || coordinate.back() == 'z'));

    if (is_combination || is_stokes_string) {
        bool stokes_ok(false);

        std::string stokes_string;
        if (is_stokes_string) {
            stokes_string = coordinate;
        } else {
            stokes_string = coordinate.substr(0, coordinate.size() - 1);
        }

        if (StokesStringTypes.count(stokes_string)) {
            CARTA::PolarizationType stokes_type = StokesStringTypes[stokes_string];
            if (_loader->GetStokesTypeIndex(stokes_type, stokes_index)) {
                stokes_ok = true;
            } else if (IsComputedStokes(stokes_string)) {
                stokes_index = StokesStringTypes.at(stokes_string);
                stokes_ok = true;
            } else {
                int assumed_stokes_index = (StokesValues[stokes_type] - 1) % 4;
                if (NumStokes() > assumed_stokes_index) {
                    stokes_index = assumed_stokes_index;
                    stokes_ok = true;
                    spdlog::warn("Can not get stokes index from the header. Assuming stokes {} index is {}.", stokes_string, stokes_index);
                }
            }
        }
        if (!stokes_ok) {
            spdlog::error("Spectral or spatial requirement {} failed: invalid stokes axis for image.", coordinate);
            return false;
        }
    } else {
        stokes_index = CurrentStokes(); // current stokes
    }
    return true;
}

std::string Frame::GetStokesType(int stokes_index) {
    for (auto stokes_type : StokesStringTypes) {
        int tmp_stokes_index;
        if (_loader->GetStokesTypeIndex(stokes_type.second, tmp_stokes_index) && (tmp_stokes_index == stokes_index)) {
            std::string stokes = (stokes_type.first.length() == 1) ? fmt::format("Stokes {}", stokes_type.first) : stokes_type.first;
            return stokes;
        }
    }
    if (IsComputedStokes(stokes_index)) {
        CARTA::PolarizationType stokes_type = StokesTypes[stokes_index];
        if (ComputedStokesName.count(stokes_type)) {
            return ComputedStokesName[stokes_type];
        }
    }
    return "Unknown";
}

std::shared_mutex& Frame::GetActiveTaskMutex() {
    return _active_task_mutex;
}

void Frame::InitImageHistogramConfigs() {
    _image_histogram_configs.clear();

    // set histogram requirements for the image
    HistogramConfig config;
    config.coordinate = "z"; // current stokes type
    config.channel = CURRENT_Z;
    config.num_bins = AUTO_BIN_SIZE;
    _image_histogram_configs.push_back(config);
}

void Frame::CloseCachedImage(const std::string& file) {
    if (_loader->GetFileName() == file) {
        _loader->CloseImageIfUpdated();
    }
}

bool Frame::SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message) {
    VectorFieldSettings new_settings(message);
    if (_vector_field_settings != new_settings) {
        _vector_field_settings = new_settings;
        return true;
    }
    return false;
}

bool Frame::GetDownsampledRasterData(
    std::vector<float>& data, int& downsampled_width, int& downsampled_height, int z, int stokes, CARTA::ImageBounds& bounds, int mip) {
    int tile_original_width = bounds.x_max() - bounds.x_min();
    int tile_original_height = bounds.y_max() - bounds.y_min();
    if (tile_original_width * tile_original_height == 0) {
        return false;
    }

    downsampled_width = std::ceil((float)tile_original_width / mip);
    downsampled_height = std::ceil((float)tile_original_height / mip);
    std::vector<float> tile_data;
    bool use_loader_downsampled_data(false);

    // Check does the (HDF5) loader has the right (mip) downsampled data
    if (_loader->HasMip(mip) && _loader->GetDownsampledRasterData(data, z, stokes, bounds, mip, _image_mutex)) {
        return true;
    } else {
        // Check is there another downsampled data that we can use to downsample
        for (int sub_mip = 2; sub_mip < mip; ++sub_mip) {
            if (mip % sub_mip == 0) {
                int loader_mip = mip / sub_mip;
                if (_loader->HasMip(loader_mip) &&
                    _loader->GetDownsampledRasterData(tile_data, z, stokes, bounds, loader_mip, _image_mutex)) {
                    use_loader_downsampled_data = true;
                    // Reset mip
                    mip = sub_mip;
                    // Reset the original tile width and height
                    tile_original_width = std::ceil((float)tile_original_width / loader_mip);
                    tile_original_height = std::ceil((float)tile_original_height / loader_mip);
                    break;
                }
            }
        }
    }

    if (!use_loader_downsampled_data) {
        // Get full resolution raster tile data
        int x_min = bounds.x_min();
        int x_max = bounds.x_max() - 1;
        int y_min = bounds.y_min();
        int y_max = bounds.y_max() - 1;

        auto tile_stokes_section = GetImageSlicer(AxisRange(x_min, x_max), AxisRange(y_min, y_max), AxisRange(z), stokes);
        tile_data.resize(tile_stokes_section.slicer.length().product());
        if (!GetSlicerData(tile_stokes_section, tile_data.data())) {
            return false;
        }
    }

    // Get downsampled raster tile data by block averaging
    data.resize(downsampled_height * downsampled_width);
    return BlockSmooth(
        tile_data.data(), data.data(), tile_original_width, tile_original_height, downsampled_width, downsampled_height, 0, 0, mip);
}

} // namespace carta
