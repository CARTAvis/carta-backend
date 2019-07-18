// RegionProfiler.h: class for handling requested profiles for an axis (x, y, z) and stokes

#ifndef CARTA_BACKEND_REGION_REGIONPROFILER_H_
#define CARTA_BACKEND_REGION_REGIONPROFILER_H_

#include <string>
#include <utility>
#include <vector>

#include <carta-protobuf/region_requirements.pb.h>

namespace carta {

struct SpatialProfile {
    std::string profile;
    std::pair<int, int> profile_axes; // <axis index, stokes index>
    bool profile_sent;
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
    // return false if profileIndex out of range:
    bool GetSpectralConfigStokes(int& stokes, int profile_index);
    std::string GetSpectralCoordinate(int profile_index);
    bool GetSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profile_index);

private:
    // parse spatial/coordinate strings into <axisIndex, stokesIndex> pairs
    std::pair<int, int> GetAxisStokes(std::string profile);
    // determine unsent profiles by diffing current profiles with last profiles
    void DiffSpatialRequirements(std::vector<SpatialProfile>& last_profiles);

    // spatial profile: map coordinate string to <axis, stokes> pair and whether data has been sent 
    std::vector<SpatialProfile> _spatial_profiles;

    // spectral
    std::vector<int> _spectral_stokes;
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> _spectral_configs;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONPROFILER_H_
