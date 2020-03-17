// RegionHandler.h: class for handling requirements and data streams for regions

#ifndef CARTA_BACKEND_REGION_REGIONHANDLER_H_
#define CARTA_BACKEND_REGION_REGIONHANDLER_H_

#include <vector>

#include <tbb/atomic.h>

#include <carta-protobuf/region_requirements.pb.h>

#include "../Frame.h"
#include "../Requirements.h"
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

    // Requirements
    bool SetHistogramRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& configs);
    bool SetSpectralRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs);
    bool SetStatsRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<int>& stats_types);
    void RemoveFrame(int file_id);

    // Calculations
    bool FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id);
    bool FillSpectralProfileData(
        std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed);
    bool FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id);

    // Handle jobs so handler is not deleted until finished
    void IncreaseZProfileCount(int file_id, int region_id);
    void DecreaseZProfileCount(int file_id, int region_id);

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

    // Clear requirements for closed region(s)
    void ClearRequirements(int region_id);
    bool SpectralConfigExists(int region_id, int file_id, SpectralConfig spectral_config);

    // Fill data stream messages
    bool CheckRegionFileIds(int region_id, int file_id);
    bool ApplyRegionToFile(int region_id, int file_id, const ChannelRange& channel, int stokes, casacore::ImageRegion& region);
    bool FillRegionFileHistogramData(
        int region_id, int file_id, std::vector<HistogramConfig>& configs, CARTA::RegionHistogramData& histogram_message);
    bool GetRegionFileSpectralData(int region_id, int file_id, SpectralConfig& spectral_config,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback);
    bool FillRegionFileStatsData(int region_id, int file_id, std::vector<int>& required_stats, CARTA::RegionStatsData& stats_message);

    // Logging
    bool _verbose;

    // Trigger job cancellation when true
    volatile bool _cancel_all_jobs = false;

    // Track ongoing calculations
    tbb::atomic<int> _z_profile_count;

    // Regions: key is region_id
    std::unordered_map<int, std::unique_ptr<Region>> _regions;

    // Frames: key is file_id
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;

    // Requirements: key is region_id
    std::unordered_map<int, std::vector<RegionHistogramConfig>> _histogram_req;
    std::unordered_map<int, std::vector<RegionSpectralConfig>> _spectral_req;
    std::unordered_map<int, std::vector<RegionStatsConfig>> _stats_req;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONHANDLER_H_
