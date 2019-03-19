//# Region.cc: implementation of class for managing a region

#include "Region.h"
#include <casacore/lattices/LRegions/LCExtension.h>
#include <algorithm> // max
#include <cmath>  // round

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type, int minchan, int maxchan,
        const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition imageShape, int spectralAxis, int stokesAxis) :
        m_name(name),
        m_type(type),
        m_rotation(0.0),
        m_minchan(-1),
        m_maxchan(-1),
        m_valid(false),
        m_xyRegionChanged(false),
        m_spectralChanged(false),
        m_latticeShape(imageShape),
        m_spectralAxis(spectralAxis),
        m_stokesAxis(stokesAxis),
        m_xyAxes(casacore::IPosition(2, 0, 1)),
        m_xyRegion(nullptr) {
    // validate and set region parameters
    if (updateRegionParameters(minchan, maxchan, points, rotation)) {
        m_valid = true;
        m_stats = std::unique_ptr<RegionStats>(new RegionStats());
        m_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::~Region() {
    if (m_xyRegion)
        delete m_xyRegion;
    m_stats.reset();
    m_profiler.reset();
}

bool Region::updateRegionParameters(int minchan, int maxchan, const std::vector<CARTA::Point>&points,
    float rotation) {
    // Set region parameters and flags for what changed
    bool xyParamsChanged((pointsChanged(points) || (rotation != m_rotation)));
    bool spectralParamsChanged((minchan != m_minchan) || (maxchan != m_maxchan));

    // validate and set params
    bool pointsSet(setPoints(points));
    bool chansSet(setChannelRange(minchan, maxchan));
    m_rotation = rotation;

    // region changed if xy params changed and validated
    m_xyRegionChanged = xyParamsChanged && pointsSet;

    // spectral changed if chan params changed and validated
    m_spectralChanged = spectralParamsChanged && chansSet;

    return pointsSet && chansSet;
}

// *************************************************************************
// Region settings

bool Region::setPoints(const std::vector<CARTA::Point>& points) {
    // check and set control points 
    bool pointsUpdated(false);
    if (checkPoints(points)) {
        m_ctrlpoints = points;
        pointsUpdated = true;
    }
    return pointsUpdated;
}

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
    // check if NaN or inf points
    bool pointsOK(false);
    if (points.size() == 1) { // (x, y)
        float x(points[0].x()), y(points[0].y());
        pointsOK = (std::isfinite(x) && std::isfinite(y));
    }
    return pointsOK; 
}

bool Region::checkRectanglePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points, width/height less than 0
    bool pointsOK(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), width(points[1].x()), height(points[1].y());
        bool pointsExist = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(width) && std::isfinite(height));
        pointsOK = (pointsExist && (width > 0) && (height > 0));
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


// ***********************************
// Lattice Region with parameters applied

bool Region::getRegion(casacore::LatticeRegion& region, int stokes) {
    // Return LatticeRegion for given stokes and region parameters.
    // if stokes = -1, used stored stokes (image region only)
    bool regionOK(false);
    if (stokes < 0)
        return regionOK;

    if (m_latticeShape.size()==2) {
        region = casacore::LatticeRegion(*m_xyRegion);
        regionOK = true;
    } else {  // extend 2D region by chan range, stokes
        casacore::LCRegion* lcregion = makeExtendedRegion(stokes);
        if (lcregion) {
            region = casacore::LatticeRegion(lcregion);
            regionOK = true;
        }
    }
    return regionOK;
}

casacore::LCRegion* Region::makeExtendedRegion(int stokes) {
    // Return 2D lattice region extended by chan range, stokes
    // Returns nullptr if 2D image
    size_t ndim(m_latticeShape.size());
    casacore::LCRegion* region(nullptr);
    if (!isValid() || (ndim==2) || !m_xyRegion)
        return region;

    // create extension box for channel/stokes
    casacore::LCBox extBox;
    if (!makeExtensionBox(extBox, stokes))
        return region;  // no need for extension

    // specify 0-based extension axes, either 2 (3D image) or 2 and 3 (4D image)
    casacore::IPosition extAxes = (ndim == 3 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
    // apply extension box to extension axes with xy region
    region = new casacore::LCExtension(*m_xyRegion, extAxes, extBox); 
    return region;
}

bool Region::makeXYRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // create 2D casacore::LCRegion for type using stored control points and rotation
    bool xyRegionOK(false);
    casacore::LCRegion* region(nullptr);
    try {
        switch(m_type) {
            case CARTA::RegionType::POINT: {
                region = makePointRegion(points);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                region = makeRectangleRegion(points, rotation);
                    break;
                }
        /*
            case CARTA::RegionType::ELLIPSE: {
                regionUpdated = makeEllipseRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                regionUpdated = makePolygonRegion(points);
                break;
            }
        */
            default:
                break;
        }
    } catch (casacore::AipsError& err) { // lattice region failed
        return false;
    }
    if (region) {
        // the xy region does not change unless region params updated
        if (m_xyRegion)
            delete m_xyRegion;
        m_xyRegion = region;
        xyRegionOK = true;
    }
    return xyRegionOK;
}

casacore::LCRegion* Region::makePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 LCBox
    casacore::LCBox* box(nullptr);
    if (points.size()==1) {
        auto x = points[0].x();
        auto y = points[0].y();
        try {
            casacore::IPosition blc(2, x, y), trc(2, x, y);
            box = new casacore::LCBox(blc, trc, m_latticeShape.keepAxes(m_xyAxes));
        } catch (casacore::AipsError& err) {
            // LC box failed
        }
    }
    return box;
}

casacore::LCRegion* Region::makeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // width x height LCBox
    casacore::LCBox* box(nullptr);
    if ((points.size()==2) && (rotation == 0.0)) { // TODO support rotation
        float cx(points[0].x()), cy(points[0].y());
        float width(points[1].x()), height(points[1].y());
        try {
            casacore::IPosition start(2), count(2, width, height);
            start(0) = std::max(0.0, cx - std::round(width/2.0));
            start(1) = std::max(0.0, cy - std::round(height/2.0));
            casacore::Slicer slicer(start, count);
            box = new casacore::LCBox(slicer, m_latticeShape.keepAxes(m_xyAxes));
        } catch (casacore::AipsError& err) {
            // LCBox failed
        }
    }
    return box;
}

bool Region::makeExtensionBox(casacore::LCBox& extendBox, int stokes) {
    // Create extension box for stored channel range and given stokes.
    // This can change for different profile/histogram/stats requirements so not stored 
    if (m_latticeShape.size() < 3)
        return false; // not needed

    casacore::IPosition start(m_latticeShape), count(m_latticeShape), boxShape(m_latticeShape);
    if (m_spectralAxis > 0) {
        start(m_spectralAxis) = m_minchan;
        count(m_spectralAxis) = (m_maxchan - m_minchan) + 1;
    }
    if (m_stokesAxis > 0) {
        start(m_stokesAxis) = stokes;
        count(m_stokesAxis) = 1;
    }
    // remove x,y axes from IPositions
    casacore::IPosition extBoxStart = start.removeAxes(m_xyAxes);
    casacore::IPosition extBoxCount= count.removeAxes(m_xyAxes);
    casacore::IPosition extBoxShape = boxShape.removeAxes(m_xyAxes);
    // make extension box from slicer
    casacore::Slicer extSlicer(extBoxStart, extBoxCount);
    extendBox = casacore::LCBox(extSlicer, extBoxShape);
    return true;
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

