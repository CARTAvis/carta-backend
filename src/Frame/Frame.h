/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

#include "Cache/RequirementsCache.h"
#include "Cache/TileCache.h"
#include "DataStream/Contouring.h"
#include "DataStream/Tile.h"
#include "DataStream/VectorField.h"
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

static std::unordered_map<CARTA::PolarizationType, std::string> ComputedStokesName{
    {CARTA::PolarizationType::Ptotal, "Total polarization intensity"}, {CARTA::PolarizationType::Plinear, "Linear polarization intensity"},
    {CARTA::PolarizationType::PFtotal, "Fractional total polarization intensity"},
    {CARTA::PolarizationType::PFlinear, "Fractional linear polarization intensity"},
    {CARTA::PolarizationType::Pangle, "Polarization angle"}};

class Frame {
public:
    // Load image cache for default_z, except for PV preview image which needs cube
    Frame(uint32_t session_id, std::shared_ptr<FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z,
        int reserved_memory = 0);
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
    bool SetHistogramRequirements(int region_id, const std::vector<CARTA::HistogramConfig>& histogram_configs);
    bool FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> region_histogram_callback, int region_id,
        int file_id, bool channel_changed);
    bool GetBasicStats(int z, int stokes, BasicStats<float>& stats);
    bool CalculateHistogram(int region_id, int z, int stokes, int num_bins, const HistogramBounds& bounds, Histogram& hist);
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
    bool GetRegionSubImage(const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image);
    bool GetSlicerSubImage(const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image);
    // Returns data vector
    bool GetRegionData(const StokesRegion& stokes_region, std::vector<float>& data, bool report_performance = true);
    bool GetSlicerData(const StokesSlicer& stokes_slicer, float* data);
    // Returns stats_values map for spectral profiles and stats data
    bool GetRegionStats(const StokesRegion& stokes_region, const std::vector<CARTA::StatsType>& required_stats, bool per_z,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    bool GetSlicerStats(const StokesSlicer& stokes_slicer, std::vector<CARTA::StatsType>& required_stats, bool per_z,
        std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    // Spectral profiles from loader
    bool UseLoaderSpectralData(const casacore::IPosition& region_shape);
    bool GetLoaderPointSpectralData(std::vector<float>& profile, int stokes, CARTA::Point& point);
    bool GetLoaderSpectralData(int region_id, const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress);

    // Moments calculation
    bool CalculateMoments(int file_id, GeneratorProgressCallback progress_callback, const StokesRegion& stokes_region,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results,
        RegionState region_state = RegionState());
    void StopMomentCalc();

    // Image fitting
    bool FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response, GeneratedImage& model_image,
        GeneratedImage& residual_image, GeneratorProgressCallback progress_callback, StokesRegion* stokes_region = nullptr);
    void StopFitting();

    // Save as a new file or export sub-image to CASA/FITS format
    void SaveFile(const std::string& root_folder, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack,
        std::shared_ptr<Region> image_region);

    bool GetStokesTypeIndex(const string& coordinate, int& stokes_index);
    std::string GetStokesType(int stokes_index);

    std::shared_mutex& GetActiveTaskMutex();

    // Get image interface ptr
    inline std::shared_ptr<casacore::ImageInterface<float>> GetImage() {
        return _loader->GetImage();
    }

    // Close image with cached data
    void CloseCachedImage(const std::string& file);

    // For vector field setting and calculation
    bool SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message);
    bool GetDownsampledRasterData(
        std::vector<float>& data, int& downsampled_width, int& downsampled_height, int z, int stokes, CARTA::ImageBounds& bounds, int mip);
    bool CalculateVectorField(const std::function<void(CARTA::VectorOverlayTileData&)>& callback);

    int UsedReservedMemory() const;

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
    bool FillHistogramFromLoaderCache(int z, int stokes, int num_bins, CARTA::Histogram* histogram); // histogram message
    bool FillHistogramFromFrameCache(
        int z, int stokes, int num_bins, const HistogramBounds& bounds, CARTA::Histogram* histogram);              // histogram message
    bool GetCachedImageHistogram(int z, int stokes, int num_bins, const HistogramBounds& bounds, Histogram& hist); // internal histogram
    bool GetCachedCubeHistogram(int stokes, int num_bins, const HistogramBounds& bounds, Histogram& hist);         // internal histogram

    // Check for cancel
    bool HasSpectralConfig(const SpectralConfig& config);

    // Export image
    bool ExportCASAImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message);
    bool ExportFITSImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_filename, casacore::String& message);
    void ValidateChannelStokes(std::vector<int>& channels, std::vector<int>& stokes, const CARTA::SaveFile& save_file_msg);
    casacore::Slicer GetExportImageSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape);
    casacore::Slicer GetExportRegionSlicer(const CARTA::SaveFile& save_file_msg, casacore::IPosition image_shape,
        casacore::IPosition region_shape, casacore::LattRegionHolder& latt_region_holder);

    // For convenience, create int map key for storing cache by z and stokes
    inline int CacheKey(int z, int stokes) {
        return (z * 10) + stokes;
    }

    // For vector field calculation
    bool DoVectorFieldCalculation(const std::function<void(CARTA::VectorOverlayTileData&)>& callback);

    // Get the start index of cube image cache, if any
    long long int StartIdxOfCubeImageCache(int z_index = CURRENT_Z) const;

    // Get image cache index (-1 for current channel and stokes, or stokes indices 0, 1, 2, or 3, except for computed stokes indices)
    int ImageCacheIndex(int stokes_index = CURRENT_STOKES) const;

    bool GetImageCache(int image_cache_index, int z_index = ALL_Z);

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
    int _x_axis, _y_axis, _z_axis; // X and Y are render axes, Z is depth axis (non-render axis) that is not stokes (if any)
    int _spectral_axis, _stokes_axis;
    int _z_index, _stokes_index; // current index
    size_t _width, _height, _depth, _num_stokes;

    // Image settings
    CARTA::AddRequiredTiles _required_animation_tiles;

    // Current cursor position
    PointXy _cursor;

    // Contour settings
    ContourSettings _contour_settings;

    // Image data cache and mutex
    long long int _image_cache_size;
    // Map of image caches
    // key = -1: image cache of the current channel and stokes data
    // key > -1: image cache of all channels data with respect to the stokes index, e.g., 0, 1, 2, or 3 (except for computed stokes indices)
    std::unordered_map<int, std::unique_ptr<float[]>> _image_caches;
    bool _cube_image_cache;        // if true, cache the whole cube image. Otherwise, only cache a channel image
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
    int _moment_name_index;

    // Image fitter
    std::unique_ptr<ImageFitter> _image_fitter;

    // Vector field settings
    VectorField _vector_field;
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_H_
