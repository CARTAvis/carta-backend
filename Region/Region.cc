//# Region.cc: implementation of class for managing a region

#include "Region.h"
//#include <carta-protobuf/defs.pb.h>

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type) :
    m_name(name), m_type(type), m_rotation(0.0) {
    m_stats = std::unique_ptr<RegionStats>(new RegionStats());
    m_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
}

void Region::setChannels(int minchan, int maxchan, std::vector<int>& stokes) {
    m_minchan = minchan;
    m_maxchan = maxchan;
    m_stokes = stokes;
}

void Region::setControlPoints(const std::vector<CARTA::Point>& points) {
    m_ctrlpoints = points;
}

void Region::setRotation(const float rotation) {
    m_rotation = rotation;
}

std::vector<CARTA::Point> Region::getControlPoints() {
    return m_ctrlpoints;
}

// ***********************************
// RegionStats

// histogram

bool Region::setHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogramReqs) {
    m_stats->setHistogramRequirements(histogramReqs);
    return true;
}

CARTA::SetHistogramRequirements_HistogramConfig Region::getHistogramConfig(int histogramIndex) {
    return m_stats->getHistogramConfig(histogramIndex);
}

size_t Region::numHistogramConfigs() {
    return m_stats->numHistogramConfigs();
}

void Region::fillHistogram(CARTA::Histogram* histogram, const casacore::Matrix<float>& chanMatrix,
        const size_t chanIndex, const size_t stokesIndex) {
    return m_stats->fillHistogram(histogram, chanMatrix, chanIndex, stokesIndex);
}

// stats
void Region::setStatsRequirements(const std::vector<int>& statsTypes) {
    m_stats->setStatsRequirements(statsTypes);
}

void Region::fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice) {
    m_stats->fillStatsData(statsData, subLattice);
}

// ***********************************
// RegionProfiler

// spatial

bool Region::setSpatialRequirements(const std::vector<std::string>& profiles,
        const int nstokes, const int defaultStokes) {
    return m_profiler->setSpatialRequirements(profiles, nstokes, defaultStokes);
}

size_t Region::numSpatialProfiles() {
    return m_profiler->numSpatialProfiles();
}

std::pair<int,int> Region::getSpatialProfileReq(int profileIndex) {
    return m_profiler->getSpatialProfileReq(profileIndex);
}

std::string Region::getSpatialProfileStr(int profileIndex) {
    return m_profiler->getSpatialProfileStr(profileIndex);
}

// spectral

bool Region::setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs,
        const int nstokes, const int defaultStokes) {
    return m_profiler->setSpectralRequirements(configs, nstokes, defaultStokes);
}

size_t Region::numSpectralProfiles() {
    return m_profiler->numSpectralProfiles();
}

bool Region::getSpectralConfigStokes(int& stokes, int profileIndex) {
    return m_profiler->getSpectralConfigStokes(stokes, profileIndex);
}

void Region::fillProfileStats(int profileIndex, CARTA::SpectralProfileData& profileData,
    casacore::SubLattice<float>& lattice) {
    // Fill SpectralProfileData with statistics values according to config stored in RegionProfiler;
    // RegionStats does calculations
    CARTA::SetSpectralRequirements_SpectralConfig config;
    if (m_profiler->getSpectralConfig(config, profileIndex)) {
        std::string coordinate(config.coordinate());
        casacore::IPosition lattShape(lattice.shape());
        if (lattShape(0)==1 && lattShape(1)==1) { // cursor region, no stats computed
            auto newProfile = profileData.add_profiles();
            newProfile->set_coordinate(coordinate);
            newProfile->set_stats_type(CARTA::StatsType::None);
            // get subLattice spectral axis
            casacore::IPosition start(lattShape.size(), 0);
            casacore::IPosition count(lattShape);
            casacore::Slicer slicer(start, count);
            casacore::Array<float> buffer;
            lattice.doGetSlice(buffer, slicer);
            std::vector<float> svalues = buffer.tovector();
            *newProfile->mutable_vals() = {svalues.begin(), svalues.end()};
        } else {
            // get values from RegionStats
            const std::vector<int> requestedStats(config.stats_types().begin(), config.stats_types().end());
            size_t nstats = requestedStats.size();
            std::vector<std::vector<float>> statsValues; // a float vector for each stats type
            if (m_stats->getStatsValues(statsValues, requestedStats, lattice)) {
                for (size_t i=0; i<nstats; ++i) {
                    // one SpectralProfile per stats type
                    auto newProfile = profileData.add_profiles();
                    newProfile->set_coordinate(coordinate);
                    auto statType = static_cast<CARTA::StatsType>(requestedStats[i]);
                    newProfile->set_stats_type(statType);
                    std::vector<float> svalues(statsValues[i]);
                    *newProfile->mutable_vals() = {svalues.begin(), svalues.end()};
                }
            }
        }
    }
}

