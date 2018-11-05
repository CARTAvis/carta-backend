// RegionProfiler.h: class for creating requested profiles for and axis (x, y, z) and stokes

#pragma once

#include <vector>
#include <utility>
#include <string>

#include <carta-protobuf/region_requirements.pb.h>

namespace carta {

class RegionProfiler {

public:

    // spatial
    bool setSpatialRequirements(const std::vector<std::string>& profiles,
        const int nstokes, const int defaultStokes);
    size_t numSpatialProfiles();
    std::pair<int,int> getSpatialProfileReq(int profileIndex);
    std::string getSpatialProfileStr(int profileIndex);

    // spectral
    bool setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles,
        const int nstokes, const int defaultStokes);
    size_t numSpectralProfiles();
    // return false if profileIndex out of range:
    bool getSpectralConfigStokes(int& stokes, int profileIndex);
    bool getSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profileIndex);

private:
    // parse spatial/coordinate strings into <axisIndex, stokesIndex> pairs
    std::pair<int, int> getAxisStokes(std::string profile);

    // spatial
    std::vector<std::string> m_spatialProfiles;
    std::vector<std::pair<int, int>> m_profilePairs;

    // spectral
    std::vector<int> m_spectralStokes;
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> m_spectralConfigs;
};

}
