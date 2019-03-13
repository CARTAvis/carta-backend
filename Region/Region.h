//# Region.h: class for managing a region

#pragma once

#include "RegionStats.h"
#include "RegionProfiler.h"
#include <carta-protobuf/spectral_profile.pb.h>

namespace carta {

class Region {

// Region could be:
// * the 3D cube for a given stokes
// * the 2D image for a given channel, stokes
// * a 1-pixel cursor (point) for a given x, y, and stokes, and all channels
// * a CARTA::Region type

public:
    Region(const std::string& name, const CARTA::RegionType type, int minchan, int maxchan,
        const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition imageShape, int spectralAxis, int stokesAxis);
    ~Region();

    // to determine if data needs to be updated
    inline bool isValid() { return m_valid; };
    inline bool isPoint() { return (m_type==CARTA::POINT); };
    inline bool regionChanged() { return m_regionChanged; };
    inline bool spectralChanged() { return m_spectralChanged; };

    // set/get Region parameters
    bool updateRegionParameters(int minchan, int maxchan, const std::vector<CARTA::Point>& points, float rotation);
    bool setChannelRange(int minchan, int maxchan);
    bool setStokes(int stokes);
    inline std::vector<CARTA::Point> getControlPoints() { return m_ctrlpoints; };

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

    // create/update Region with current xy parameters
    bool setRegion(const std::vector<CARTA::Point>&points, float rotation);
    bool makePointRegion(const CARTA::Point& point);
    bool makeRectangleRegion(const std::vector<CARTA::Point>&points, float rotation);
    //bool makeEllipseRegion(const std::vector<CARTA::Point>&points, float rotation);
    //bool makePolygonRegion(const std::vector<CARTA::Point>&points);

    // bounds checking for Region parameters
    bool checkPixelPoint(const CARTA::Point& point);
    bool checkWidthHeight(const CARTA::Point& point);
    bool pointsChanged(const std::vector<CARTA::Point>& newpoints); // compare new points with stored points
    bool checkChannelRange(int& minchan, int& maxchan);
    bool checkStokes(int& stokes);
    
    // region definition (ICD SET_REGION parameters)
    std::string m_name;
    CARTA::RegionType m_type;
    std::vector<CARTA::Point> m_ctrlpoints;
    int m_minchan, m_maxchan, m_stokes;
    float m_rotation;

    // region flags
    bool m_valid;
    bool m_regionChanged, m_spectralChanged;

    // image shape info
    casacore::IPosition m_latticeShape;
    int m_spectralAxis, m_stokesAxis;

    // Lattice-Coordinate Regions
    std::unique_ptr<casacore::LCRegion> m_region;

    // classes for requirements, calculations
    std::unique_ptr<carta::RegionStats> m_stats;
    std::unique_ptr<carta::RegionProfiler> m_profiler;
};

} // namespace carta
