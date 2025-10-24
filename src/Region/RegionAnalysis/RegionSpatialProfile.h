/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionSpatialProfile.h: class for handling requirements and data streams for region spatial profiles

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_REGIONSPATIALPROFILE_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_REGIONSPATIALPROFILE_H_

#include <vector>

#include "Cache/RequirementsCache.h"
#include "Frame/Frame.h"

namespace carta {

class RegionSpatialProfile {
public:
    /** @brief Default constructor. */
    RegionSpatialProfile() = default;

    /**
     * @brief Constructor which sets spatial configurations.
     * @param region_id Region id for spatial profiles
     * @param file_id File id for image frame
     * @param configs Spatial configurations
     */
    RegionSpatialProfile(int region_id, int file_id, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs);

    /**
     * @brief Set spatial profile configurations.
     * @param file_id File id for image frame
     * @param configs Spatial configurations
     */
    void SetConfigurations(int file_id, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs);

    /**
     * @brief Get spatial configurations for file id.
     * @param[in] file_id File id for image frame
     * @param[out] configs Spatial configurations
     * @return Whether configs exist for file id
     */
    bool GetConfigurations(int file_id, std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs);

    /**
     * @brief Get file ids in configurations which match file id.
     * @param file_id File id for image frame
     * @return File ids
     */
    std::vector<int> GetConfigFileIds(int file_id);

    /**
     * @brief Add calculated region spatial profile for point region to message.
     * @param[in] file_id File id for image frame
     * @param[in] frame Image frame
     * @param[in] configs Spatial configurations
     * @param[in] lc_region Region applied to image
     * @param[out] spatial_profile_messages Region spatial profile data messages
     * @return Whether profiles were added
     */
    bool GetPointSpatialProfile(int file_id, std::shared_ptr<Frame> frame,
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs, std::shared_ptr<casacore::LCRegion> lc_region,
        std::vector<CARTA::SpatialProfileData>& spatial_profile_messages);

    /**
     * @brief Add calculated spatial profile for line region to message.
     * @param[in] file_id File id for image frame
     * @param[in] frame Image frame
     * @param[in] region Line or polyline region
     * @param[in] stokes Stokes axis index
     * @param[in] z Z axis index
     * @param[in] config Spatial configuration
     * @param[out] cancelled Whether line profile was cancelled
     * @param[out] message Message if line profile failed
     * @param[out] spatial_profile_message Region spatial profile data message
     * @return Whether profile was added
     */
    bool GetLineSpatialProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> region, int stokes, int z,
        CARTA::SetSpatialRequirements_SpatialConfig& config, bool& cancelled, std::string& message,
        CARTA::SpatialProfileData& spatial_profile_message);

    /**
     * @brief Clear configurations for frame.
     * @param file_id File id for image frame
     */
    void ClearFileConfigs(int file_id);

private:
    /**
     * @brief Check if configuration exists with input parameters, for line profile cancellation.
     * @param file_id File id for image frame
     * @param[in] config Spatial configuration
     * @return Whether configuration exists
     */
    bool HasConfiguration(int file_id, CARTA::SetSpatialRequirements_SpatialConfig& config);

    /**
     * @brief Calculate profile for line region by approximating as a box region for each pixel.
     * @param[in] file_id File id for image frame
     * @param[in] frame Image frame
     * @param[in] region Line or polyline region
     * @param[in] stokes Stokes axis index
     * @param[in] z Z axis index
     * @param[in] config Spatial configuration
     * @param[out] cancelled Whether profile was cancelled
     * @param[out] message Error message
     * @param[out] profile Result of profile calculation
     * @param[out] increment Increment of spatial axis in profile
     * @return Whether profile calculation completed
     */
    bool GetLineProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> region, int stokes, int z,
        CARTA::SetSpatialRequirements_SpatialConfig& config, bool& cancelled, std::string& message, casacore::Vector<float>& profile,
        casacore::Quantity& increment);

    /**
     * @brief Calculate mean value for box region along line.
     * @param file_id File id for image frame
     * @param frame Image frame
     * @param box_region_state Box region parameters
     * @param box_csys Coordinate system used to define box
     * @param z Z axis index
     * @param stokes Stokes axis index
     * @return Mean value of box region
     */
    float GetBoxMeanValue(int file_id, std::shared_ptr<Frame> frame, RegionState& box_region_state,
        std::shared_ptr<casacore::CoordinateSystem> box_csys, int z, int stokes);

    /**
     * @brief Check whether to cancel line profile calculation
     * @param file_id File id for image frame
     * @param frame Image frame
     * @param line_region Line or polyline region
     * @param z Z axis index
     * @param line_region_state Line region parameters when profile started
     * @param config Spatial profile configuration
     * @return Whether to cancel
     */
    bool CancelLineProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> line_region, int z,
        RegionState& line_region_state, CARTA::SetSpatialRequirements_SpatialConfig& config);

    /** @brief Region id, for config id. */
    int _region_id;

    /** @brief Spatial profile configurations. */
    std::unordered_map<ConfigId, std::vector<CARTA::SetSpatialRequirements_SpatialConfig>, ConfigIdHash> _configs;

    /** @brief Lock to add/remove configurations. */
    std::mutex _config_mutex;

    /** @brief Lock for calculating during line profiles, for cancellation. */
    std::mutex _profile_mutex;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_REGIONSPATIALPROFILE_H_
