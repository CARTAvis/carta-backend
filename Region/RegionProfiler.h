// RegionProfiler.h: class for handling requested profiles for an axis (x, y, z) and stokes

#ifndef CARTA_BACKEND_REGION_REGIONPROFILER_H_
#define CARTA_BACKEND_REGION_REGIONPROFILER_H_

#include <string>
#include <utility>
#include <vector>

#include "../Util.h"

#include <carta-protobuf/region_requirements.pb.h>

namespace carta {

struct SpatialProfile {
    std::string coordinate;
    std::pair<int, int> profile_axes; // <axis index, stokes index>
    bool profile_sent;
};

struct SpectralProfile {
    std::string coordinate;
    SpectralConfig config;
    std::vector<bool> profiles_sent;
};

class RegionProfiler {
public:
    // spatial
    bool SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes);
    size_t NumSpatialProfiles();
    std::string GetSpatialCoordinate(int profile_index);
    std::pair<int, int> GetSpatialProfileAxes(int profile_index);
    bool GetSpatialProfileSent(int profile_index);
    void SetSpatialProfileSent(int profile_index, bool sent);
    void SetAllSpatialProfilesUnsent(); // enable sending new profiles

    // spectral
    bool SetSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles, const int num_stokes);
    size_t NumSpectralProfiles();
    bool IsValidSpectralConfig(const SpectralConfig& config);
    std::vector<SpectralProfile> GetSpectralProfiles();
    std::string GetSpectralCoordinate(int config_stokes);
    bool GetSpectralConfig(int config_stokes, SpectralConfig& config);
    bool GetUnsentStatsForProfile(int config_stokes, std::vector<int>& stats);
    bool GetSpectralProfileAllStatsSent(int config_stokes);
    void SetSpectralProfileStatSent(int config_stokes, int stats_type, bool sent);
    void SetSpectralProfileAllStatsSent(int config_stokes, bool sent);
    void SetAllSpectralProfilesUnsent(); // enable sending new profile data, e.g. when region changes

private:
    // parse coordinate strings into <axisIndex, stokesIndex> pairs
    std::pair<int, int> CoordinateToAxisStokes(std::string coordinate);

    // determine unsent profiles by diffing current profiles with last profiles
    void DiffSpatialRequirements(std::vector<SpatialProfile>& last_profiles);
    void DiffSpectralRequirements(std::vector<SpectralProfile>& last_profiles);

    // profiles
    std::vector<SpatialProfile> _spatial_profiles;
    std::vector<SpectralProfile> _spectral_profiles;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONPROFILER_H_
