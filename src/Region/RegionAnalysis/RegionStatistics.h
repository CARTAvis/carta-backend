/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionStatistics.h: class for handling requirements and data streams for region statistics

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_REGIONSTATISTICS_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_REGIONSTATISTICS_H_

#include <cstdint>
#include <mutex>
#include <vector>

#include "Cache/RequirementsCache.h"
#include "Frame/Frame.h"

namespace carta {

class RegionStatistics {
public:
    /** @brief Default constructor. */
    RegionStatistics() = default;

    /**
     * @brief Constructor which sets stats configurations.
     * @param region_id Region id for statistics
     * @param file_id File id for image frame
     * @param configs Stats configurations
     */
    RegionStatistics(int region_id, int file_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs);

    /**
     * @brief Set statistics configurations.
     * @param file_id File id for image frame
     * @param configs Stats configurations
     */
    void SetConfigurations(int file_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs);

    /**
     * @brief Get statistics configurations for file id.
     * @param[in] file_id File id for image frame
     * @param[out] configs Statistics configurations
     * @return Whether configs exist for file id
     */
    bool GetConfigurations(int file_id, std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs);

    /**
     * @brief Get file ids in configurations which match file id.
     * @param file_id File id for image frame
     * @return File ids
     */
    std::vector<int> GetConfigFileIds(int file_id);

    /**
     * @brief Get cache generation, which changes when the cache is cleared.
     * @details Must be read before getting the region to pass to GetRegionStatsData.
     * @return Cache generation
     */
    std::uint64_t GetCacheGeneration();

    /**
     * @brief Add cached or calculated statistics to message.
     * @param[in] file_id File id for image frame
     * @param[in] frame Image frame
     * @param[in] config Statistics configuration
     * @param[in] stokes_region Struct holding image region and struct describing stokes and z range
     * @param[in] cache_generation Cache generation before stokes_region was created; results are not cached if it has changed
     * @param[in, out] stats_data_message Region stats data message
     * @return Whether statistics were added
     */
    bool GetRegionStatsData(int file_id, std::shared_ptr<Frame> frame, const CARTA::SetStatsRequirements_StatsConfig& config,
        StokesRegion& stokes_region, std::uint64_t cache_generation, CARTA::RegionStatsData& stats_data_message);

    /** @brief Clear cache when region changes. */
    void ClearCache();

    /** @brief Clear configurations and cache for file id. */
    void ClearFileConfigsCache(int file_id);

private:
    /**
     * @brief Add statistics from cache if it exists.
     * @param[in] cache_id CacheId struct
     * @param[in] config Stats configuration
     * @param[in, out] stats_data_message Region stats data message
     * @return Whether cached stats was added
     */
    bool AddCachedStatistics(
        CacheId& cache_id, const CARTA::SetStatsRequirements_StatsConfig& config, CARTA::RegionStatsData& stats_data_message);

    /** @brief Region id for this object, for config and cache ids. */
    int _region_id;

    /** @brief Requirements map. */
    std::unordered_map<ConfigId, RegionStatsConfig> _configs;

    /** @brief Lock for _configs. Never held together with _cache_mutex. */
    std::mutex _config_mutex;

    /** @brief Lock for _cache and _cache_generation; never held during statistics calculation. */
    std::mutex _cache_mutex;

    /** @brief Cache to hold calculations. */
    std::unordered_map<CacheId, StatsCache> _cache;

    /** @brief Detect cache invalidation after getting the region for a statistics calculation. */
    std::uint64_t _cache_generation = 0;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_REGIONSTATISTICS_H_
