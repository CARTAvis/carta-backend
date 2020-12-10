/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

#include "../Frame.h"
#include "../RequirementsCache.h"
#include "Region.h"

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

namespace carta {

class RegionHandler {
public:
    RegionHandler(bool perflog);

    // Regions
    bool SetRegion(int& region_id, RegionState& region_state, casacore::CoordinateSystem* csys);
    bool RegionChanged(int region_id);
    void RemoveRegion(int region_id);

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
    bool SetStatsRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::StatsType>& stats_types);

    // Calculations
    bool FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id);
    bool FillSpectralProfileData(
        std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed);
    bool FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id);

    // Calculate moments
    bool CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame, MomentProgressCallback progress_callback,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
        std::vector<carta::CollapseResult>& collapse_results);

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

    // Spectral requirements helpers
    // Check if stokes is valid (e.g. frontend can send "Vz" for 3-stokes image)
    bool SpectralCoordinateValid(std::string& coordinate, int nstokes);
    // Check if spectral config has been changed/cancelled
    bool HasSpectralRequirements(int region_id, int file_id, std::string& coordinate, std::vector<CARTA::StatsType>& required_stats);
    // Set all requirements "new" when region changes
    void UpdateNewSpectralRequirements(int region_id);

    // Fill data stream messages
    bool RegionFileIdsValid(int region_id, int file_id);
    casacore::LCRegion* ApplyRegionToFile(int region_id, int file_id);
    bool ApplyRegionToFile(int region_id, int file_id, const ChannelRange& channel, int stokes, casacore::ImageRegion& region);
    bool GetRegionHistogramData(
        int region_id, int file_id, std::vector<HistogramConfig>& configs, CARTA::RegionHistogramData& histogram_message);
    bool GetRegionSpectralData(int region_id, int file_id, std::string& coordinate, int stokes_index,
        std::vector<CARTA::StatsType>& required_stats,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback);
    bool GetRegionStatsData(
        int region_id, int file_id, std::vector<CARTA::StatsType>& required_stats, CARTA::RegionStatsData& stats_message);

    // Logging
    bool _perflog;

    // Trigger job cancellation when true
    volatile bool _cancel_all_jobs = false;

    // Track ongoing calculations
    std::atomic<int> _z_profile_count;

    // Regions: key is region_id
    std::unordered_map<int, std::shared_ptr<Region>> _regions;

    // Frames: key is file_id
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;

    // Requirements; ConfigId key contains file, region
    std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> _histogram_req;
    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> _spectral_req;
    std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> _stats_req;
    std::mutex _spectral_mutex;

    // Cache; CacheId key contains file, region, stokes, (optional) channel
    std::unordered_map<CacheId, HistogramCache, CacheIdHash> _histogram_cache;
    std::unordered_map<CacheId, SpectralCache, CacheIdHash> _spectral_cache;
    std::unordered_map<CacheId, StatsCache, CacheIdHash> _stats_cache;

    std::vector<CARTA::StatsType> _spectral_stats = {CARTA::StatsType::Sum, CARTA::StatsType::FluxDensity, CARTA::StatsType::Mean,
        CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max};
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONHANDLER_H_
