//# Region.h: class for managing a region
// Region could be:
// * the entire image
// * a point
// * a region

#pragma once

#include "RegionStats.h"
#include "RegionProfiler.h"
#include <carta-protobuf/spectral_profile.pb.h>

namespace carta {

class Region {

public:
    Region(const std::string& name, const CARTA::RegionType type);
    ~Region();

    // set Region parameters
    void setChannels(int minchan, int maxchan, const std::vector<int>& stokes);
    void setControlPoints(const std::vector<CARTA::Point>& points);
    void setRotation(const float rotation);
    // get Region parameters
    std::vector<CARTA::Point> getControlPoints();

    // Histogram: pass through to RegionStats
    bool setHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogramReqs);
    CARTA::SetHistogramRequirements_HistogramConfig getHistogramConfig(int histogramIndex);
    size_t numHistogramConfigs();
    bool getMinMax(int channel, int stokes, float& minVal, float& maxVal);
    void setMinMax(int channel, int stokes, float minVal, float maxVal);
    void calcMinMax(int channel, int stokes, const std::vector<float>& data, float& minVal, float& maxVal);
    bool getHistogram(int channel, int stokes, int nbins, CARTA::Histogram& histogram);
    void setHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void calcHistogram(int channel, int stokes, int nBins, float minVal, float maxVal,
        const std::vector<float>& data, CARTA::Histogram& histogramMsg);

    // Spatial: pass through to RegionProfiler
    bool setSpatialRequirements(const std::vector<std::string>& profiles, const int nstokes);
    size_t numSpatialProfiles();
    std::pair<int,int> getSpatialProfileReq(int profileIndex);
    std::string getSpatialProfileStr(int profileIndex);

    // Spectral: pass through to RegionProfiler
    bool setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles,
        const int nstokes);
    size_t numSpectralProfiles();
    bool getSpectralConfigStokes(int& stokes, int profileIndex);
    bool getSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profileIndex);
    void fillProfileStats(int profileIndex, CARTA::SpectralProfileData& profileData, 
        casacore::SubLattice<float>& lattice);

    // Stats: pass through to RegionStats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    size_t numStats();
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice);

private:

    // region definition (ICD SET_REGION)
    std::string m_name;
    CARTA::RegionType m_type;
    int m_minchan, m_maxchan;
    std::vector<int> m_stokes;
    std::vector<CARTA::Point> m_ctrlpoints;
    float m_rotation;

    std::unique_ptr<carta::RegionStats> m_stats;
    std::unique_ptr<carta::RegionProfiler> m_profiler;
};

} // namespace carta
