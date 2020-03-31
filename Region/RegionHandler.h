// RegionHandler.h: class for handling requirements and data streams for regions

#ifndef CARTA_BACKEND_REGION_REGIONHANDLER_H_
#define CARTA_BACKEND_REGION_REGIONHANDLER_H_

#include <vector>

#include <tbb/atomic.h>

#include <carta-protobuf/region_requirements.pb.h>

#include "../Frame.h"
#include "../RequirementsCache.h"
#include "Region.h"

namespace carta {

class RegionHandler {
public:
    RegionHandler(bool verbose);

    // Regions
    bool SetRegion(int& region_id, int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points,
        float rotation, const casacore::CoordinateSystem& csys);
    bool RegionChanged(int region_id);
    void RemoveRegion(int region_id);

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

private:
    // Get unique region id (max id + 1)
    int GetNextRegionId();

    // Check specific id or if any regions/frames set
    bool RegionSet(int region_id);
    bool FrameSet(int file_id);

    // Cancel jobs for specific region or all regions before closing region/handler
    void CancelJobs(int region_id);
    void CancelAllJobs();
    bool ShouldCancelJob(int region_id);

    // Clear requirements for closed region(s) or file(s)
    void RemoveRegionRequirementsCache(int region_id);
    void RemoveFileRequirementsCache(int file_id);
    // Clear cache for changed region
    void ClearRegionCache(int region_id);

    // Check if spectral config has been cancelled
    bool HasSpectralRequirements(int region_id, int file_id, int stokes);
    // Set all requirements "new" when region changes
    void UpdateNewSpectralRequirements(int region_id);

    // Fill data stream messages
    bool RegionFileIdsValid(int region_id, int file_id);
    bool ApplyRegionToFile(int region_id, int file_id, const ChannelRange& channel, int stokes, casacore::ImageRegion& region);
    bool GetRegionHistogramData(
        int region_id, int file_id, std::vector<HistogramConfig>& configs, CARTA::RegionHistogramData& histogram_message);
    bool GetRegionSpectralData(int region_id, int file_id, int config_stokes, int stokes_index,
        std::vector<CARTA::StatsType>& required_stats,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback);
    bool GetRegionStatsData(
        int region_id, int file_id, std::vector<CARTA::StatsType>& required_stats, CARTA::RegionStatsData& stats_message);

    // Logging
    bool _verbose;

    // Trigger job cancellation when true
    volatile bool _cancel_all_jobs = false;

    // Track ongoing calculations
    tbb::atomic<int> _z_profile_count;

    // Regions: key is region_id
    std::unordered_map<int, std::shared_ptr<Region>> _regions;

    // Frames: key is file_id
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;

    // Requirements; ConfigId key contains file, region
    std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> _histogram_req;
    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> _spectral_req;
    std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> _stats_req;

    // Cache; CacheId key contains file, region, stokes, (optional) channel
    std::unordered_map<CacheId, HistogramCache, CacheIdHash> _histogram_cache;
    std::unordered_map<CacheId, SpectralCache, CacheIdHash> _spectral_cache;
    std::unordered_map<CacheId, StatsCache, CacheIdHash> _stats_cache;

    std::vector<CARTA::StatsType> _spectral_stats = {CARTA::StatsType::Sum, CARTA::StatsType::FluxDensity, CARTA::StatsType::Mean,
        CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max};
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONHANDLER_H_
