/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHandler.h: class for handling requirements and data streams for regions

#ifndef CARTA_BACKEND_REGION_REGIONHANDLER_H_
#define CARTA_BACKEND_REGION_REGIONHANDLER_H_

#include <vector>

#include <carta-protobuf/export_region.pb.h>
#include <carta-protobuf/import_region.pb.h>
#include <carta-protobuf/region_requirements.pb.h>

#include "Cache/RequirementsCache.h"
#include "Frame/Frame.h"
#include "ImageGenerators/PvGenerator.h"
#include "Region.h"

namespace carta {

struct RegionStyle {
    std::string name;
    std::string color;
    int line_width;
    std::vector<int> dash_list;

    RegionStyle() {}
    RegionStyle(const std::string& name_, const std::string& color_, int line_width_, const std::vector<int> dash_list_)
        : name(name_), color(color_), line_width(line_width_), dash_list(dash_list_) {}
};

struct RegionProperties {
    RegionProperties() {}
    RegionProperties(RegionState& region_state, RegionStyle& region_style) : state(region_state), style(region_style) {}

    RegionState state;
    RegionStyle style;
};

class RegionHandler {
public:
    RegionHandler() = default;

    // Regions
    bool SetRegion(int& region_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> csys);
    bool RegionChanged(int region_id);
    void RemoveRegion(int region_id);
    std::shared_ptr<Region> GetRegion(int region_id);

    // Region Import/Export
    void ImportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type, const std::string& region_file,
        bool file_is_filename, CARTA::ImportRegionAck& import_ack);
    void ExportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type, CARTA::CoordinateType coord_type,
        std::map<int, CARTA::RegionStyle>& region_styles, std::string& filename, CARTA::ExportRegionAck& export_ack);

    // Frames
    void RemoveFrame(int file_id);

    // Requirements
    bool SetHistogramRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& configs);
    bool SetSpectralRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs);
    bool SetStatsRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetStatsRequirements_StatsConfig>& stats_configs);

    // Calculations
    bool FillRegionHistogramData(
        std::function<void(CARTA::RegionHistogramData histogram_data)> region_histogram_callback, int region_id, int file_id);
    bool FillSpectralProfileData(
        std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed);
    bool FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id);

    // Calculate moments
    bool CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results);

    // Point regions
    bool SetSpatialRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_profiles);
    bool FillSpatialProfileData(int file_id, int region_id, std::vector<CARTA::SpatialProfileData>& spatial_data_vec);
    bool IsPointRegion(int region_id);
    std::vector<int> GetPointRegionIds(int file_id);
    std::vector<int> GetProjectedFileIds(int region_id);

    // Generate PV image
    bool CalculatePvImage(int file_id, int region_id, int width, std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, GeneratedImage& pv_image);
    void StopPvCalc(int file_id);

private:
    // Get unique region id (max id + 1)
    int GetNextRegionId();

    // Check specific id or if any regions/frames set
    bool RegionSet(int region_id);
    bool FrameSet(int file_id);

    // Clear requirements for closed region(s) or file(s)
    void RemoveRegionRequirementsCache(int region_id);
    void RemoveFileRequirementsCache(int file_id);
    // Clear cache for changed region
    void ClearRegionCache(int region_id);

    // Check if spectral config has been changed/cancelled
    bool HasSpectralRequirements(int region_id, int file_id, std::string& coordinate, std::vector<CARTA::StatsType>& required_stats);
    // Set all requirements "new" when region changes
    void UpdateNewSpectralRequirements(int region_id);

    // Apply region to image
    bool RegionFileIdsValid(int region_id, int file_id);
    std::shared_ptr<casacore::LCRegion> ApplyRegionToFile(
        int region_id, int file_id, const StokesSrc& stokes_src = StokesSrc(), bool report_error = true);
    bool ApplyRegionToFile(int region_id, int file_id, const AxisRange& z_range, int stokes,
        std::pair<StokesSrc, casacore::ImageRegion>& stokes_region, std::shared_ptr<casacore::LCRegion> region_2D);

    // Fill data stream messages
    bool GetRegionHistogramData(int region_id, int file_id, const std::vector<HistogramConfig>& configs,
        std::vector<CARTA::RegionHistogramData>& histogram_messages);
    bool GetRegionSpectralData(int region_id, int file_id, std::string& coordinate, int stokes_index,
        std::vector<CARTA::StatsType>& required_stats, bool report_error,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback);
    bool GetRegionStatsData(
        int region_id, int file_id, int stokes, const std::vector<CARTA::StatsType>& required_stats, CARTA::RegionStatsData& stats_message);

    // Generate box regions to approximate a line with a width, and get mean of each box (per z else current z).
    // Used for pv generator and spatial profiles.
    bool GetLineProfiles(int file_id, int region_id, int width, bool per_z, std::function<void(float)>& progress_callback,
        double& increment, casacore::Matrix<float>& profiles, bool& cancelled, std::string& message);
    void SetLineRotation(RegionState& region_state);
    bool GetFixedPixelRegionProfiles(int file_id, int width, bool per_z, RegionState& region_state,
        std::shared_ptr<casacore::CoordinateSystem> reference_csys, std::function<void(float)>& progress_callback,
        casacore::Matrix<float>& profiles, double& increment, bool& cancelled);
    bool CheckLinearOffsets(
        const std::vector<CARTA::Point>& box_centers, std::shared_ptr<casacore::CoordinateSystem> csys, double& increment);
    double GetSeparationTolerance(std::shared_ptr<casacore::CoordinateSystem> csys);
    bool GetFixedAngularRegionProfiles(int file_id, int width, bool per_z, RegionState& region_state,
        std::shared_ptr<casacore::CoordinateSystem> reference_csys, std::function<void(float)>& progress_callback,
        casacore::Matrix<float>& profiles, double& increment, bool& cancelled, std::string& message);
    bool SetPointInRange(float max_point, float& point);
    casacore::Vector<double> FindPointAtTargetSeparation(const casacore::DirectionCoordinate& direction_coord,
        const casacore::Vector<double>& endpoint0, const casacore::Vector<double>& endpoint1, double target_separation, double tolerance);
    RegionState GetTemporaryRegionState(casacore::DirectionCoordinate& direction_coord, int file_id,
        const casacore::Vector<double>& box_start, const casacore::Vector<double>& box_end, int pixel_width, double angular_width,
        float height_angle, double tolerance);
    casacore::Vector<float> GetTemporaryRegionProfile(
        int file_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> csys, bool per_z, double& num_pixels);

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

    // Regions: key is region_id
    std::unordered_map<int, std::shared_ptr<Region>> _regions;

    // Frames: key is file_id
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;

    // Requirements; ConfigId key contains file, region
    std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> _histogram_req;
    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> _spectral_req;
    std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> _stats_req;
    std::mutex _spectral_mutex;

    // Cache; CacheId key contains file, region, stokes, (optional) z index
    std::unordered_map<CacheId, HistogramCache, CacheIdHash> _histogram_cache;
    std::unordered_map<CacheId, SpectralCache, CacheIdHash> _spectral_cache;
    std::unordered_map<CacheId, StatsCache, CacheIdHash> _stats_cache;

    // Spectral profiles to calculate with ImageStatistics.
    // TODO: Remove NumPixels, for pvgen testing
    std::vector<CARTA::StatsType> _spectral_stats = {CARTA::StatsType::Sum, CARTA::StatsType::FluxDensity, CARTA::StatsType::Mean,
        CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max,
        CARTA::StatsType::Extrema, CARTA::StatsType::NumPixels};

    // Point regions configs, key is region id
    std::unordered_map<ConfigId, std::vector<CARTA::SetSpatialRequirements_SpatialConfig>, ConfigIdHash> _spatial_req;
    std::mutex _spatial_mutex;

    // PV cancellation: key is file_id
    std::unordered_map<int, bool> _stop_pv;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONHANDLER_H_
