/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHistogram.h: class for storing requirements and calculating histograms for regions

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_REGIONHISTOGRAM_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_REGIONHISTOGRAM_H_

#include "Cache/RequirementsCache.h"
#include "Frame/Frame.h"

namespace carta {

class RegionHistogram {
public:
    /** @brief Default constructor. */
    RegionHistogram() = default;

    /**
     * @brief Constructor which sets histogram requirements.
     * @param region_id Region id for histogram
     * @param file_id File id for image frame
     * @param configs Histogram configurations
     */
    RegionHistogram(int region_id, int file_id, const std::vector<CARTA::HistogramConfig>& configs);

    /**
     * @brief Set configurations for histogram calculation.
     * @param file_id File id for image frame
     * @param configs Histogram configuration structs
     */
    void SetConfigurations(int file_id, const std::vector<CARTA::HistogramConfig>& configs);

    /**
     * @brief Get configurations for histogram calculation for file id.
     * @param[in] file_id File id for image frame
     * @param[out] configs Histogram configuration structs
     * @return Whether configs exist for file id
     */
    bool GetConfigurations(int file_id, std::vector<HistogramConfig>& configs);

    /**
     * @brief Get file ids in configurations which match file id.
     * @param file_id File id for image frame
     * @return File ids
     */
    std::vector<int> GetConfigFileIds(int file_id);

    /**
     * @brief Add cached or calculated histogram to message.
     * @param[in] file_id File id for image frame
     * @param[in] frame Image frame
     * @param[in] config Histogram configuration struct
     * @param[in] lcregion Region applied to image
     * @param[in] stokes_source Struct describing stokes and z range
     * @param[out] histogram_data_message Region histogram data message
     * @return Whether histogram was added
     */
    bool GetRegionHistogramData(int file_id, std::shared_ptr<Frame> frame, const HistogramConfig& config,
        std::shared_ptr<casacore::LCRegion> lcregion, StokesSource& stokes_source, CARTA::RegionHistogramData& histogram_data_message);

    /** @brief Clear cache when region changes. */
    void ClearCache();

    /**
     * @brief Clear configurations and cache for frame.
     * @param file_id File id for image frame
     */
    void ClearFileConfigsCache(int file_id);

private:
    /**
     * @brief Fill message with histogram parameters.
     * @param[in] file_id File id for image frame
     * @param[in] stokes_source Struct describing stokes and z range
     * @param[in] config Histogram configuration struct
     * @param[out] histogram_data_message Region histogram data message
     */
    void FillHistogramDataParams(
        int file_id, StokesSource& stokes_source, const HistogramConfig& config, CARTA::RegionHistogramData& histogram_data_message);

    /**
     * @brief Get number of bins from config and calculate if not supplied.
     * @param config Histogram configuration struct
     * @param frame Image frame
     * @param lcregion Region applied to image
     * @return number of bins
     */
    int GetNumBins(const HistogramConfig& config, std::shared_ptr<Frame> frame, std::shared_ptr<casacore::LCRegion> lcregion);

    /**
     * @brief Add Histogram from cache if it exists.
     * @param[in] cache_id CacheId struct
     * @param[in] config Histogram configuration
     * @param[in] num_bins Number of histogram bins
     * @param[in, out] histogram_data_message Region histogram data message
     * @return Whether cached histogram was added
     */
    bool AddCachedHistogram(
        CacheId& cache_id, const HistogramConfig& config, int num_bins, CARTA::RegionHistogramData& histogram_data_message);

    /**
     * @brief Add default histogram to message.
     * @param[in, out] histogram_data Histogram data message
     */
    void AddDefaultHistogram(CARTA::RegionHistogramData& histogram_data);

    /** @brief Region id for this object, for config and cache ids. */
    int _region_id;

    /** @brief Histogram configurations. */
    std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> _configs;

    /** @brief Cache to hold calculated histograms. */
    std::unordered_map<CacheId, HistogramCache, CacheIdHash> _cache;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_REGIONHISTOGRAM_H_
