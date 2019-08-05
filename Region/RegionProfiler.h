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
    int stokes_index;
    std::vector<int> stats_types;
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
    int NumStatsToLoad(int profile_index);
    int GetSpectralConfigStokes(int profile_index);
    std::string GetSpectralCoordinate(int profile_index);
    bool GetSpectralConfigStats(int profile_index, ZProfileWidget& stats); // all requested
    bool IsValidSpectralConfigStats(const ZProfileWidget& stats);
    bool GetSpectralStatsToLoad(int profile_index, std::vector<int>& stats); // diff of requested and already sent
    bool GetSpectralProfileStatSent(int profile_index, int stats_type);
    void SetSpectralProfileStatSent(int profile_index, int stats_type, bool sent);
    void SetSpectralProfileAllStatsSent(int profile_index, bool sent);
    void SetAllSpectralProfilesUnsent(); // enable sending new profiles

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
