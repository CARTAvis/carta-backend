// RegionProfiler.h: class for creating requested profiles for and axis (x, y, z) and stokes

#ifndef CARTA_BACKEND_REGION_REGIONPROFILER_H_
#define CARTA_BACKEND_REGION_REGIONPROFILER_H_

#include <string>
#include <utility>
#include <vector>

#include <carta-protobuf/region_requirements.pb.h>

namespace carta {

class RegionProfiler {
public:
    // spatial
    bool SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes);
    size_t NumSpatialProfiles();
    std::pair<int, int> GetSpatialProfileReq(int profile_index);
    std::string GetSpatialCoordinate(int profile_index);

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

    // spatial
    std::vector<std::string> _spatial_profiles;
    std::vector<std::pair<int, int>> _profile_pairs;

    // spectral
    std::vector<int> _spectral_stokes;
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> _spectral_configs;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONPROFILER_H_
