// RegionProfiler.cc: implementation of RegionProfiler class to create x, y, z region profiles

#include "RegionProfiler.h"

using namespace carta;

// ***** spatial *****

bool RegionProfiler::setSpatialRequirements(const std::vector<std::string>& profiles,
            const int nstokes, const int defaultStokes) {
    // process profile strings into pairs <axis, stokes>
    m_spatialProfiles.clear();
    m_profilePairs.clear();
    for (auto profile : profiles) {
        if (profile.empty() || profile.size() > 2) // ignore invalid profile string
            continue;
        // convert string to pair<axisIndex, stokesIndex>;
        std::pair<int, int> axisStokes = getAxisStokes(profile);
        if ((axisStokes.first < 0) || (axisStokes.first > 1)) // invalid axis
            continue;
        if (axisStokes.second > (nstokes-1)) // invalid stokes
            continue;
        else if (axisStokes.second < 0) // not specified
            axisStokes.second = defaultStokes;
        m_spatialProfiles.push_back(profile);
        m_profilePairs.push_back(axisStokes);
    }
    return (profiles.size()==m_spatialProfiles.size());
}

std::pair<int, int> RegionProfiler::getAxisStokes(std::string profile) {
    // converts profile string into <axis, stokes> pair
    int axisIndex(-1), stokesIndex(-1);
    // axis
    char axisChar(profile.back());
    if (axisChar=='x') axisIndex = 0;
    else if (axisChar=='y') axisIndex = 1; 
    else if (axisChar=='z') axisIndex = 2; 
    // stokes
    if (profile.size()==2) {
        char stokesChar(profile.front());
        if (stokesChar=='I') stokesIndex=0;
        else if (stokesChar=='Q') stokesIndex=1;
        else if (stokesChar=='U') stokesIndex=2;
        else if (stokesChar=='V') stokesIndex=3;
    }
    return std::make_pair(axisIndex, stokesIndex);
}

size_t RegionProfiler::numSpatialProfiles() {
    return m_profilePairs.size();
}

std::pair<int,int> RegionProfiler::getSpatialProfileReq(int profileIndex) {
    if (profileIndex < m_profilePairs.size())
        return m_profilePairs[profileIndex];
    else
        return std::pair<int,int>();
}

std::string RegionProfiler::getSpatialProfileStr(int profileIndex) {
    if (profileIndex < m_spatialProfiles.size())
        return m_spatialProfiles[profileIndex];
    else
        return std::string();
}

// ***** spectral *****

bool RegionProfiler::setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs,
        const int nstokes, const int defaultStokes) {
    // parse stokes into index
    m_spectralConfigs.clear();
    m_spectralStokes.clear();
    for (auto config : configs) {
        std::string coordinate(config.coordinate());
        if (coordinate.empty() || coordinate.size() > 2) // ignore invalid profile string
            continue;
        std::pair<int, int> axisStokes = getAxisStokes(coordinate);
        if (axisStokes.first != 2)  // invalid axis
            continue;
        if (axisStokes.second > (nstokes-1)) // invalid stokes
            continue;
        int stokes;
        if (axisStokes.second < 0) { // use default
            stokes = defaultStokes;
        } else {
            stokes = axisStokes.second;
        }
        m_spectralConfigs.push_back(config);
        m_spectralStokes.push_back(stokes);
    }
    return (configs.size()==m_spectralConfigs.size());
}

size_t RegionProfiler::numSpectralProfiles() {
    return m_spectralConfigs.size();
}

bool RegionProfiler::getSpectralConfigStokes(int& stokes, int profileIndex) {
    // return Stokes int value at given index; return false if index out of range
    bool indexOK(false);
    if (profileIndex < m_spectralStokes.size()) {
        stokes = m_spectralStokes[profileIndex];
        indexOK = true;
    }
    return indexOK;
}

bool RegionProfiler::getSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profileIndex) {
    // return SpectralConfig at given index; return false if index out of range
    bool indexOK(false);
    if (profileIndex < m_spectralConfigs.size()) {
        config = m_spectralConfigs[profileIndex];
        indexOK = true;
    }
    return indexOK;
}

