/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHandler.h: class for handling requirements and data streams for regions

#ifndef CARTA_SRC_REGION_REGIONHANDLER_H_
#define CARTA_SRC_REGION_REGIONHANDLER_H_

#include <mutex>
#include <vector>

#include "Cache/RequirementsCache.h"
#include "Frame/Frame.h"
#include "ImageGenerators/PvGenerator.h"
#include "ImageGenerators/PvPreviewCube.h"
#include "ImageGenerators/PvPreviewCut.h"
#include "Region.h"
#include "RegionAnalysis/RegionHistogram.h"
#include "RegionAnalysis/RegionSpatialProfile.h"
#include "RegionAnalysis/RegionStatistics.h"

namespace carta {

class RegionHandler {
public:
    RegionHandler() = default;
    ~RegionHandler();

    // Regions
    bool SetRegion(int& region_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> csys);
    void RemoveRegion(int region_id);
    std::shared_ptr<Region> GetRegion(int region_id);

    // Region Import/Export
    void ImportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type, const std::string& region_file,
        bool file_is_filename, CARTA::ImportRegionAck& import_ack);
    void ExportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type, CARTA::CoordinateType coord_type,
        std::map<int, CARTA::RegionStyle>& region_styles, std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack);

    // Frames
    void RemoveFrame(int file_id);

    // Requirements
    bool SetHistogramRequirements(
        int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::HistogramConfig>& configs);
    bool SetSpatialRequirements(
        int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs);
    bool SetSpectralRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs);
    bool SetStatsRequirements(
        int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs);

    // Calculations
    bool FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id);
    bool FillSpectralProfileData(
        std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed);
    bool FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id);
    bool FillSpatialProfileData(std::function<void(CARTA::SpatialProfileData profile_data)> cb, int file_id, int region_id);

    // Calculate moments
    bool CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results);

    // Tests for region type, and not annotation
    bool IsPointRegion(int region_id);
    bool IsLineRegion(int region_id);
    bool IsClosedRegion(int region_id);

    // Generate PV image or preview image
    bool CalculatePvImage(const CARTA::PvRequest& pv_request, std::shared_ptr<Frame> frame, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, GeneratedImage& pv_image);
    // Update PV preview image when PV cut region (region_id) changes
    bool UpdatePvPreviewRegion(int region_id, RegionState& region_state);
    bool UpdatePvPreviewImage(
        int file_id, int region_id, bool quick_update, std::function<void(CARTA::PvResponse& pv_response, GeneratedImage& pv_image)> cb);
    void StopPvCalc(int file_id);
    void StopPvPreview(int preview_id);
    void StopPvPreviewUpdates(int preview_id);
    void ClosePvPreview(int preview_id);

    // Image fitting
    bool FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response, std::shared_ptr<Frame> frame,
        GeneratedImage& model_image, GeneratedImage& residual_image, GeneratorProgressCallback progress_callback);

private:
    // Region ID handling
    int GetNextRegionId();
    int GetNextTemporaryRegionId();

    // Check specific id or if any regions/frames set
    bool RegionFileIdsValid(int region_id, int file_id, bool check_annotation = false);
    bool RegionSet(int region_id, bool check_annotation = false);
    bool FrameSet(int file_id);

    // Requirements helpers
    bool HasSpectralRequirements(
        int region_id, int file_id, const std::string& coordinate, const std::vector<CARTA::StatsType>& required_stats);
    void UpdateNewSpectralRequirements(int region_id);
    void RemoveRegionRequirementsCache(int region_id);
    void RemoveFileRequirementsCache(int file_id);
    void ClearRegionCache(int region_id);

    // Data stream helpers
    bool GetRegionSpectralData(int region_id, int file_id, const AxisRange& z_range, std::string& coordinate, int stokes_index,
        std::vector<CARTA::StatsType>& required_stats, bool report_error,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback);
    bool GetLineSpatialData(int file_id, int region_id, const std::string& coordinate, int stokes_index, int width,
        const std::function<void(std::vector<float>&, casacore::Quantity&)>& spatial_profile_callback);

    // Generate box regions to approximate a line with a width, and get mean of each box for z-range for PV image.
    bool GetLineProfiles(int file_id, int region_id, int width, const AxisRange& z_range, int stokes_index, const std::string& coordinate,
        std::function<void(float)>& progress_callback, casacore::Matrix<float>& profiles, casacore::Quantity& increment, bool& cancelled,
        std::string& message, bool reverse = false);
    bool CancelLineProfiles(int region_id, int file_id, RegionState& region_state);
    casacore::Vector<float> GetTemporaryRegionProfile(int file_id, RegionState& region_state,
        std::shared_ptr<casacore::CoordinateSystem> csys, const AxisRange& z_range, int stokes_index, double& num_pixels);

    // Get computed stokes profiles for a region
    using ProfilesMap = std::map<CARTA::StatsType, std::vector<double>>;
    void GetStokesPtotal(
        const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, const ProfilesMap& profiles_v, ProfilesMap& profiles_ptotal);
    void GetStokesPftotal(const ProfilesMap& profiles_i, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u,
        const ProfilesMap& profiles_v, ProfilesMap& profiles_pftotal);
    void GetStokesPlinear(const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_plinear);
    void GetStokesPflinear(
        const ProfilesMap& profiles_i, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_pflinear);
    void GetStokesPangle(const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_pangle);
    void CombineStokes(ProfilesMap& profiles_out, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u,
        const std::function<double(double, double)>& func);
    void CombineStokes(ProfilesMap& profiles_out, const ProfilesMap& profiles_other, const std::function<double(double, double)>& func);
    bool IsValid(double a, double b);
    bool GetComputedStokesProfiles(
        ProfilesMap& profiles, int stokes_index, const std::function<bool(ProfilesMap&, std::string)>& get_profiles_data);

    // PV generator
    bool CalculatePvImage(int file_id, int region_id, int width, AxisRange& spectral_range, bool reverse, bool keep,
        std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
        GeneratedImage& pv_image);
    // PV preview
    int GetPvPreviewFrameId(int preview_id);
    bool CalculatePvPreviewImage(int file_id, int region_id, int width, AxisRange& spectral_range, bool reverse,
        std::shared_ptr<Frame>& frame, const CARTA::PvPreviewSettings& preview_settings, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, GeneratedImage& pv_image);
    bool CalculatePvPreviewImage(int frame_id, int preview_id, bool quick_update, std::shared_ptr<PvPreviewCut> preview_cut,
        std::shared_ptr<PvPreviewCube> preview_cube, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
        GeneratedImage& pv_image);

    // Regions: key is region_id
    std::unordered_map<int, std::shared_ptr<Region>> _regions;
    std::mutex _region_mutex;

    // Frames: key is file_id
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;

    // Region analysis
    std::unordered_map<int, std::unique_ptr<RegionHistogram>> _region_histograms;
    std::unordered_map<int, std::shared_ptr<RegionSpatialProfile>> _region_spatial_profiles;
    std::unordered_map<int, std::unique_ptr<RegionStatistics>> _region_statistics;
    std::mutex _spatial_mutex;

    // Spectral profile requirements, cache, and list of supported stats
    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> _spectral_req;
    std::unordered_map<CacheId, SpectralCache, CacheIdHash> _spectral_cache;
    std::mutex _spectral_mutex;
    std::vector<CARTA::StatsType> _spectral_stats = {CARTA::StatsType::Sum, CARTA::StatsType::FluxDensity, CARTA::StatsType::Mean,
        CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max,
        CARTA::StatsType::Extrema, CARTA::StatsType::NumPixels};

    // PV generator, key is file_id.
    std::unordered_map<int, bool> _stop_pv;      // cancel
    std::unordered_map<int, int> _pv_name_index; // name suffix for "keep"

    // PV preview, key is preview_id. Mutex to protect cut and cube in use.
    std::unordered_map<int, std::shared_ptr<PvPreviewCut>> _pv_preview_cuts;
    std::unordered_map<int, std::shared_ptr<PvPreviewCube>> _pv_preview_cubes;
    std::shared_mutex _pv_cut_mutex;
    std::shared_mutex _pv_cube_mutex;

    // Prevent crash during line profiles
    std::mutex _line_profile_mutex;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONHANDLER_H_
