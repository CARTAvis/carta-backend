//# Region.h: class for managing a region

#pragma once

#include "RegionStats.h"
#include "RegionProfiler.h"
#include "../InterfaceConstants.h"
#include <carta-protobuf/spectral_profile.pb.h>

namespace carta {

class Region {

// Region could be:
// * the 3D cube for a given stokes
// * the 2D image for a given channel, stokes
// * a 1-pixel cursor (point) for a given x, y, and stokes, and all channels
// * a CARTA::Region type

public:
    Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points,
        const float rotation, const casacore::IPosition imageShape, int spectralAxis, int stokesAxis);
    ~Region();

    // to determine if data needs to be updated
    inline bool isValid() { return m_valid; };
    inline bool isPoint() { return (m_type==CARTA::POINT); };
    inline bool regionChanged() { return m_xyRegionChanged; };

    // set/get Region parameters
    bool updateRegionParameters(const std::string name, const CARTA::RegionType type,
        const std::vector<CARTA::Point>& points, float rotation);
    inline std::vector<CARTA::Point> getControlPoints() { return m_ctrlpoints; };
    casacore::IPosition xyShape();
    inline bool xyRegionValid() { return (m_xyRegion != nullptr); };

    // get lattice region for requested stokes and (optionally) single channel
    bool getRegion(casacore::LatticeRegion& region, int stokes, int channel=ALL_CHANNELS);
    // get data from sublattice (LCRegion applied to Lattice by Frame)
    bool getData(std::vector<float>& data, casacore::SubLattice<float>& sublattice);

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
    std::string getSpatialCoordinate(int profileIndex);

    // Spectral: pass through to RegionProfiler
    bool setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles,
        const int nstokes);
    size_t numSpectralProfiles();
    bool getSpectralConfigStokes(int& stokes, int profileIndex);
    std::string getSpectralCoordinate(int profileIndex);
    bool getSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profileIndex);
    void fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex,
        std::vector<float>& spectralData);
    void fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex,
        casacore::SubLattice<float>& sublattice);

    // Stats: pass through to RegionStats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    size_t numStats();
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice);

private:

    // bounds checking for Region parameters
    bool setPoints(const std::vector<CARTA::Point>& points);
    bool checkPoints(const std::vector<CARTA::Point>& points);
    bool checkPixelPoint(const std::vector<CARTA::Point>& points);
    bool checkRectanglePoints(const std::vector<CARTA::Point>& points);
    bool checkEllipsePoints(const std::vector<CARTA::Point>& points);
    bool checkPolygonPoints(const std::vector<CARTA::Point>& points);
    bool pointsChanged(const std::vector<CARTA::Point>& newpoints); // compare new points with stored points

    // Create xy regions
    bool setXYRegion(const std::vector<CARTA::Point>& points, float rotation); // 2D plane saved as m_xyRegion
    casacore::LCRegion* makePointRegion(const std::vector<CARTA::Point>& points);
    casacore::LCRegion* makeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::LCRegion* makeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::LCRegion* makePolygonRegion(const std::vector<CARTA::Point>& points);

    // Extend xy region to make LCRegion
    bool makeExtensionBox(casacore::LCBox& extendBox, int stokes, int channel=ALL_CHANNELS); // for extended region
    casacore::LCRegion* makeExtendedRegion(int stokes, int channel=ALL_CHANNELS);  // x/y region extended chan/stokes


    // region definition (ICD SET_REGION parameters)
    std::string m_name;
    CARTA::RegionType m_type;
    std::vector<CARTA::Point> m_ctrlpoints;
    float m_rotation;
    int m_minchan, m_maxchan;

    // region flags
    bool m_valid, m_xyRegionChanged;

    // image shape info
    casacore::IPosition m_latticeShape;
    casacore::IPosition m_xyAxes; // first two axes of lattice shape, to keep or remove
    int m_spectralAxis, m_stokesAxis;

    // stored 2D region
    casacore::LCRegion* m_xyRegion;

    // classes for requirements, calculations
    std::unique_ptr<carta::RegionStats> m_stats;
    std::unique_ptr<carta::RegionProfiler> m_profiler;
};

} // namespace carta
