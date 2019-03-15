//# Region.cc: implementation of class for managing a region

#include "Region.h"

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type, int minchan, int maxchan,
        const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition imageShape, int spectralAxis, int stokesAxis) :
        m_name(name),
        m_type(type),
        m_minchan(-1),
        m_maxchan(-1),
        m_valid(false),
        m_regionChanged(false),
        m_spectralChanged(false),
        m_latticeShape(imageShape),
        m_spectralAxis(spectralAxis),
        m_stokesAxis(stokesAxis) {
    if (updateRegionParameters(minchan, maxchan, points, rotation)) {
        m_valid = true;
        m_stats = std::unique_ptr<RegionStats>(new RegionStats());
        m_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::~Region() {
    m_stats.reset();
    m_profiler.reset();
}

bool Region::updateRegionParameters(int minchan, int maxchan, const std::vector<CARTA::Point>&points,
    float rotation) {
    // change region parameters and set flags

    bool regionChanged(pointsChanged(points) || (rotation != m_rotation));
    if (regionChanged && checkPoints(points)) {  // change xy region
        m_ctrlpoints = points;
        m_rotation = rotation;
        m_regionChanged = true;
    }

    bool spectralChanged((minchan != m_minchan) || (maxchan != m_maxchan));
    if (spectralChanged && setChannelRange(minchan, maxchan)) // change spectral axis
        m_spectralChanged = true;
    return m_regionChanged || m_spectralChanged;
}

// *************************************************************************
// Region spectral/stokes settings

bool Region::setChannelRange(int minchan, int maxchan) {
    // check and set spectral axis range
    bool channelsUpdated(false);
    if (checkChannelRange(minchan, maxchan)) {
        m_minchan = minchan;
        m_maxchan = maxchan;
        channelsUpdated = true;
    }
    return channelsUpdated;
}

bool Region::setStokes(int stokes) {
    // check and set stokes axis
    bool stokesUpdated(false);
    if (checkStokes(stokes)) {
        m_stokes = stokes;
        stokesUpdated = true;
    }
    return stokesUpdated;
}

// *************************************************************************
// Parameter checking

bool Region::checkPoints(const std::vector<CARTA::Point>& points) {
    // check point values
    bool pointsOK(false);
    switch (m_type) {
        case CARTA::POINT: {
            pointsOK = checkPixelPoint(points);
            break;
            }
        case CARTA::RECTANGLE: {
            pointsOK = (checkRectanglePoints(points));
            break;
            }
        default:
            break;
    }
    return pointsOK;
}

bool Region::checkPixelPoint(const std::vector<CARTA::Point>& points) {
    // in xy axis range
    bool pointsOK(false);
    if (points.size() == 1) { // (x, y)
        float x(points[0].x()), y(points[0].y());
        pointsOK = ((x >= 0) && (x < m_latticeShape(0)) && (y >= 0) && (y < m_latticeShape(1)));
    }
    return pointsOK;
}

bool Region::checkRectanglePoints(const std::vector<CARTA::Point>& points) {
    // in xy axis range
    bool pointsOK(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        size_t max_x(m_latticeShape(0)), max_y(m_latticeShape(1));
        float cx(points[0].x()), cy(points[0].y()), width(points[1].x()), height(points[1].y());
        float xmin(cx - std::round(width/2.0)), xmax(cx + std::round(width/2.0)),
              ymin(cy - std::round(height/2.0)), ymax(cy + std::round(height/2.0));
        bool outside = (((xmin < 0) && (xmax < 0)) || ((xmin > max_x) && (xmax > max_x)) ||
                    ((ymin < 0) && (ymax < 0)) || ((ymin > max_y) && (ymax > max_y)));
        pointsOK = !outside;
    }
    return pointsOK;
}

bool Region::pointsChanged(const std::vector<CARTA::Point>& newpoints) {
    // check equality of points (no operator==)
    bool changed(newpoints.size() != m_ctrlpoints.size()); // check number of points
    if (!changed) { // check each point in vectors
        for (size_t i=0; i<newpoints.size(); ++i) {
            if ((newpoints[i].x() != m_ctrlpoints[i].x()) || 
                (newpoints[i].y() != m_ctrlpoints[i].y())) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

bool Region::checkChannelRange(int& minchan, int& maxchan) {
    // check and set min/max channels
    bool channelRangeOK(false);
    // less than 0 is full channel range
    size_t nchan(m_spectralAxis >= 0 ? m_latticeShape(m_spectralAxis) : 1);
    minchan = (minchan < 0 ? 0 : minchan);
    maxchan = (maxchan < 0 ? nchan-1 : maxchan);
    bool minchanOK((minchan >= 0) && (minchan < nchan));
    bool maxchanOK((maxchan >= 0) && (maxchan < nchan));
    if (minchanOK && maxchanOK && (minchan <= maxchan))
        channelRangeOK = true;
    return channelRangeOK;
}

bool Region::checkStokes(int& stokes) {
    size_t nstokes(m_stokesAxis >= 0 ? m_latticeShape(m_stokesAxis) : 1);
    return ((stokes >= 0) && (stokes < nstokes));
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

bool Region::getMinMax(int channel, int stokes, float& minVal, float& maxVal) {
    return m_stats->getMinMax(channel, stokes, minVal, maxVal);
}

void Region::setMinMax(int channel, int stokes, float minVal, float maxVal) {
    std::pair<float, float> vals = std::make_pair(minVal, maxVal);
    m_stats->setMinMax(channel, stokes, vals);
}

void Region::calcMinMax(int channel, int stokes, const std::vector<float>& data, float& minVal, float& maxVal) {
    m_stats->calcMinMax(channel, stokes, data, minVal, maxVal);
}

bool Region::getHistogram(int channel, int stokes, int nbins, CARTA::Histogram& histogram) {
    return m_stats->getHistogram(channel, stokes, nbins, histogram);
}

void Region::setHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    m_stats->setHistogram(channel, stokes, histogram);
}

void Region::calcHistogram(int channel, int stokes, int nBins, float minVal, float maxVal,
        const std::vector<float>& data, CARTA::Histogram& histogramMsg) {
    m_stats->calcHistogram(channel, stokes, nBins, minVal, maxVal, data, histogramMsg);
}

// stats
void Region::setStatsRequirements(const std::vector<int>& statsTypes) {
    m_stats->setStatsRequirements(statsTypes);
}

size_t Region::numStats() {
    return m_stats->numStats();
}

void Region::fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice) {
    m_stats->fillStatsData(statsData, subLattice);
}

// ***********************************
// RegionProfiler

// spatial

bool Region::setSpatialRequirements(const std::vector<std::string>& profiles, const int nstokes) {
    return m_profiler->setSpatialRequirements(profiles, nstokes);
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
        const int nstokes) {
    return m_profiler->setSpectralRequirements(configs, nstokes);
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

