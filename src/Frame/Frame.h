/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
#include <shared_mutex>
#include <unordered_map>

#include <carta-protobuf/contour.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/fitting_request.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/region_requirements.pb.h>
#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include <carta-protobuf/tiles.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <carta-protobuf/vector_overlay_tile.pb.h>

#include "Cache/RequirementsCache.h"
#include "Cache/TileCache.h"
#include "DataStream/Contouring.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "ImageFitter/ImageFitter.h"
#include "ImageGenerators/ImageGenerator.h"
#include "ImageGenerators/MomentGenerator.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "Region/Region.h"
#include "ThreadingManager/Concurrency.h"
#include "Util/FileSystem.h"
#include "Util/Image.h"
#include "Util/Message.h"
#include "VectorFieldSettings.h"

namespace carta {

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

// Map for enum CARTA:FileType to string
static std::unordered_map<CARTA::FileType, string> FileTypeString{{CARTA::FileType::CASA, "CASA"}, {CARTA::FileType::CRTF, "CRTF"},
    {CARTA::FileType::DS9_REG, "DS9"}, {CARTA::FileType::FITS, "FITS"}, {CARTA::FileType::HDF5, "HDF5"},
    {CARTA::FileType::MIRIAD, "MIRIAD"}, {CARTA::FileType::UNKNOWN, "Unknown"}};

class Frame {
public:
    Frame(uint32_t session_id, std::shared_ptr<FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z);
    ~Frame(){};

    bool IsValid();
    std::string GetErrorMessage();

    // Get the full name of image file
    std::string GetFileName();

    // Returns shared ptr to CoordinateSystem
    std::shared_ptr<casacore::CoordinateSystem> CoordinateSystem(const StokesSource& stokes_source = StokesSource());

    // Image/Frame info
    casacore::IPosition ImageShape(const StokesSource& stokes_source = StokesSource());
    size_t Width();     // length of x axis
    size_t Height();    // length of y axis
    size_t Depth();     // length of z axis
    size_t NumStokes(); // if no stokes axis, nstokes=1
    int CurrentZ();
    int CurrentStokes();
    int SpectralAxis();
    int StokesAxis();
    bool GetBeams(std::vector<CARTA::Beam>& beams);

    // Slicer to set z and stokes ranges with full xy plane
    StokesSlicer GetImageSlicer(const AxisRange& z_range, int stokes);
    StokesSlicer GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes);

    // Image view for z index
    inline void SetAnimationViewSettings(const CARTA::AddRequiredTiles& required_animation_tiles) {
        _required_animation_tiles = required_animation_tiles;
    }
    inline CARTA::AddRequiredTiles GetAnimationViewSettings() {
        return _required_animation_tiles;
    };
    bool SetImageChannels(int new_z, int new_stokes, std::string& message);

    // Cursor
    bool SetCursor(float x, float y);

    // Raster data
    bool FillRasterTileData(CARTA::RasterTileData& raster_tile_data, const Tile& tile, int z, int stokes,
        CARTA::CompressionType compression_type, float compression_quality);

    // Functions used for smoothing and contouring
    bool SetContourParameters(const CARTA::SetContourParameters& message);
    inline ContourSettings& GetContourParameters() {
        return _contour_settings;
    };
    bool ContourImage(ContourCallback& partial_contour_callback);

    // Histograms: image and cube
    bool SetHistogramRequirements(int region_id, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_configs);
    bool FillRegionHistogramData(
        std::function<void(CARTA::RegionHistogramData histogram_data)> region_histogram_callback, int region_id, int file_id);
    bool GetBasicStats(int z, int stokes, BasicStats<float>& stats);
    bool CalculateHistogram(int region_id, int z, int stokes, int num_bins, BasicStats<float>& stats, Histogram& hist);
    bool GetCubeHistogramConfig(HistogramConfig& config);
    void CacheCubeStats(int stokes, BasicStats<float>& stats);
    void CacheCubeHistogram(int stokes, Histogram& hist);

    // Stats: image
    bool SetStatsRequirements(int region_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& stats_configs);
    bool FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> stats_data_callback, int region_id, int file_id);

    // Spatial: cursor
    void SetSpatialRequirements(const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_profiles);
    bool FillSpatialProfileData(std::vector<CARTA::SpatialProfileData>& spatial_data_vec);
    bool FillSpatialProfileData(PointXy point, std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_configs,
        std::vector<CARTA::SpatialProfileData>& spatial_data_vec);

    // Spectral: cursor
    bool SetSpectralRequirements(int region_id, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_configs);
    bool FillSpectralProfileData(std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, bool stokes_changed);

    // Set the flag connected = false, in order to stop the jobs and wait for jobs finished
    void WaitForTaskCancellation();
    // Check flag if Frame is to be destroyed
    bool IsConnected();

    // Apply Region/Slicer to image (Frame manages image mutex) and get shape, data, or stats
    std::shared_ptr<casacore::LCRegion> GetImageRegion(
        int file_id, std::shared_ptr<Region> region, const StokesSource& stokes_source = StokesSource(), bool report_error = true);
    bool GetImageRegion(int file_id, const AxisRange& z_range, int stokes, StokesRegion& stokes_region);
    casacore::IPosition GetRegionShape(const StokesRegion& stokes_region);
    // Returns data vector
    bool GetRegionData(const StokesRegion& stokes_region, std::vector<float>& data);
    bool GetSlicerData(const StokesSlicer& stokes_slicer, float* data);
    // Returns stats_values map for spectral profiles and stats data
    bool GetRegionStats(const StokesRegion& stokes_region, const std::vector<CARTA::StatsType>& required_stats, bool per_z,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    bool GetSlicerStats(const StokesSlicer& stokes_slicer, std::vector<CARTA::StatsType>& required_stats, bool per_z,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    // Spectral profiles from loader
    bool UseLoaderSpectralData(const casacore::IPosition& region_shape);
    bool GetLoaderPointSpectralData(std::vector<float>& profile, int stokes, CARTA::Point& point);
    bool GetLoaderSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress);

    // Moments calculation
    bool CalculateMoments(int file_id, GeneratorProgressCallback progress_callback, const StokesRegion& stokes_region,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results);
    void StopMomentCalc();

    // Image fitting
    bool FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response);

    // Save as a new file or export sub-image to CASA/FITS format
    void SaveFile(const std::string& root_folder, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack,
        std::shared_ptr<Region> image_region);

    bool GetStokesTypeIndex(const string& coordinate, int& stokes_index);

    std::shared_mutex& GetActiveTaskMutex();

    // Get image interface ptr
    inline std::shared_ptr<casacore::ImageInterface<float>> GetImage() {
        return _loader->GetImage();
    }

    // Close image with cached data
    void CloseCachedImage(const std::string& file);

    // Polarization vector field
    bool SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message);
    inline VectorFieldSettings& GetVectorFieldParameters() {
        return _vector_field_settings;
    };
    inline void ClearVectorFieldParameters() {
        _vector_field_settings.ClearSettings();
    };
    bool GetDownsampledRasterData(
        std::vector<float>& data, int& downsampled_width, int& downsampled_height, int z, int stokes, CARTA::ImageBounds& bounds, int mip);

protected:
    // Validate z and stokes index values
    bool CheckZ(int z);
    bool CheckStokes(int stokes);

    // Check whether z or stokes has changed
    bool ZStokesChanged(int z, int stokes);

    // Cache image plane data for current z, stokes
    bool FillImageCache();
    void InvalidateImageCache();

    // Downsampled data from image cache
    bool GetRasterData(std::vector<float>& image_data, CARTA::ImageBounds& bounds, int mip, bool mean_filter = true);
    bool GetRasterTileData(std::shared_ptr<std::vector<float>>& tile_data_ptr, const Tile& tile, int& width, int& height);

    // Fill vector for given z and stokes
    void GetZMatrix(std::vector<float>& z_matrix, size_t z, size_t stokes);

    // Histograms: z is single z index or ALL_Z for cube
    int AutoBinSize();
    bool FillHistogramFromCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram);       // histogram message
    bool FillHistogramFromLoaderCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram); // histogram message
    bool FillHistogramFromFrameCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram);  // histogram message
    bool GetCachedImageHistogram(int z, int stokes, int num_bins, Histogram& hist);                  // internal histogram
    bool GetCachedCubeHistogram(int stokes, int num_bins, Histogram& hist);                          // internal histogram

    // Check for cancel
    bool HasSpectralConfig(const SpectralConfig& config);

    // Export image
    bool ExportCASAImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message);
    bool ExportFITSImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message);
    void ValidateChannelStokes(std::vector<int>& channels, std::vector<int>& stokes, const CARTA::SaveFile& save_file_msg);
    casacore::Slicer GetExportImageSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape);
    casacore::Slicer GetExportRegionSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape,
        casacore::IPosition region_shape, std::shared_ptr<casacore::LCRegion> image_region, casacore::LattRegionHolder& latt_region_holder);

    void InitImageHistogramConfigs();

    // For convenience, create int map key for storing cache by z and stokes
    inline int CacheKey(int z, int stokes) {
        return (z * 10) + stokes;
    }

    // Setup
    uint32_t _session_id;

    // Image opened
    bool _valid;
    std::string _open_image_error;

    // Trigger job cancellation when false
    volatile bool _connected = true;

    // Image loader for image type
    std::shared_ptr<FileLoader> _loader;

    // Shape and axis info: X, Y, Z, Stokes
    casacore::IPosition _image_shape;
    int _x_axis, _y_axis, _z_axis, _spectral_axis, _stokes_axis;
    int _z_index, _stokes_index; // current index
    size_t _width, _height, _depth, _num_stokes;

    // Image settings
    CARTA::AddRequiredTiles _required_animation_tiles;

    // Current cursor position
    PointXy _cursor;

    // Contour settings
    ContourSettings _contour_settings;

    // Image data cache and mutex
    //    std::vector<float> _image_cache; // image data for current z, stokes
    long long int _image_cache_size;
    std::unique_ptr<float[]> _image_cache;
    bool _image_cache_valid;       // cached image data is valid for current z and stokes
    queuing_rw_mutex _cache_mutex; // allow concurrent reads but lock for write
    std::mutex _image_mutex;       // only one disk access at a time
    bool _cache_loaded;            // channel cache is set
    TileCache _tile_cache;         // cache for full-resolution image tiles
    std::mutex _ignore_interrupt_X_mutex;
    std::mutex _ignore_interrupt_Y_mutex;

    // Use a shared lock for long time calculations, use an exclusive lock for the object destruction
    mutable std::shared_mutex _active_task_mutex;

    // Requirements
    std::vector<HistogramConfig> _image_histogram_configs;
    std::vector<HistogramConfig> _cube_histogram_configs;
    std::vector<CARTA::SetStatsRequirements_StatsConfig> _image_required_stats;
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> _cursor_spatial_configs;
    std::vector<SpectralConfig> _cursor_spectral_configs;
    std::mutex _spectral_mutex;

    // Cache maps
    // For image, key is cache key (z/stokes); for cube, key is stokes.
    std::unordered_map<int, std::vector<Histogram>> _image_histograms, _cube_histograms;
    std::unordered_map<int, BasicStats<float>> _image_basic_stats, _cube_basic_stats;
    std::unordered_map<int, std::map<CARTA::StatsType, double>> _image_stats;

    // Moment generator
    std::unique_ptr<MomentGenerator> _moment_generator;

    // Image fitter
    std::unique_ptr<ImageFitter> _image_fitter;

    // Vector field settings
    VectorFieldSettings _vector_field_settings;
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_H_
