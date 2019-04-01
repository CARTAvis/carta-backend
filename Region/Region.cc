//# Region.cc: implementation of class for managing a region

#include "Region.h"
#include "../InterfaceConstants.h"
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCEllipsoid.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <algorithm> // max
#include <cmath>  // round

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type,
        const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition imageShape, int spectralAxis, int stokesAxis) :
        m_name(name),
        m_type(type),
        m_rotation(0.0),
        m_valid(false),
        m_xyRegionChanged(false),
        m_latticeShape(imageShape),
        m_spectralAxis(spectralAxis),
        m_stokesAxis(stokesAxis),
        m_xyAxes(casacore::IPosition(2, 0, 1)),
        m_xyRegion(nullptr) {
    // validate and set region parameters
    m_valid = updateRegionParameters(name, type, points, rotation);
    if (m_valid) {
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

// *************************************************************************
// Region settings

bool Region::updateRegionParameters(const std::string name, const CARTA::RegionType type,
        const std::vector<CARTA::Point>& points, float rotation) {
    // Set region parameters and flag if xy region changed
    bool xyParamsChanged((pointsChanged(points) || (rotation != m_rotation)));

    m_name = name;
    m_type = type;
    m_rotation = rotation;
    // validate and set points, create LCRegion
    bool pointsSet(setPoints(points));
    if (pointsSet)
        setXYRegion(points, rotation);

    // region changed if xy params changed and points validated
    m_xyRegionChanged = xyParamsChanged && pointsSet;
    if (m_xyRegionChanged && m_stats)
        m_stats->clearStats(); // recalculate everything

    return pointsSet;
}

bool Region::setPoints(const std::vector<CARTA::Point>& points) {
    // check and set control points 
    bool pointsUpdated(false);
    if (checkPoints(points)) {
        m_ctrlpoints = points;
        pointsUpdated = true;
    }
    return pointsUpdated;
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
            pointsOK = checkRectanglePoints(points);
            break;
            }
        case CARTA::ELLIPSE: {
            pointsOK = checkEllipsePoints(points);
            break;
            }
        case CARTA::POLYGON: {
            pointsOK = checkPolygonPoints(points);
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

bool Region::checkEllipsePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool pointsOK(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), bmaj(points[1].x()), bmin(points[1].y());
        pointsOK = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(bmaj) && std::isfinite(bmin));
    }
    return pointsOK; 
}

bool Region::checkPolygonPoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool pointsOK(true);
    for (auto& point : points)
        pointsOK &= (std::isfinite(point.x()) && std::isfinite(point.y()));
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

// ***********************************
// Lattice Region with parameters applied

bool Region::getRegion(casacore::LatticeRegion& region, int stokes, int channel) {
    // Return LatticeRegion for given stokes and region parameters.
    bool regionOK(false);
    if (stokes < 0) // invalid
        return regionOK;

    if (m_latticeShape.size()==2) {
        region = casacore::LatticeRegion(*m_xyRegion);
        regionOK = true;
    } else {  // extend 2D region by chan range, stokes
        casacore::LCRegion* lcregion = makeExtendedRegion(stokes, channel);
        if (lcregion) {
            region = casacore::LatticeRegion(lcregion);
            regionOK = true;
        }
    } 
    return regionOK;
}

bool Region::setXYRegion(const std::vector<CARTA::Point>& points, float rotation) {
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
            case CARTA::RegionType::ELLIPSE: {
                region = makeEllipseRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                region = makePolygonRegion(points);
                break;
            }
            default:
                break;
        }
    } catch (casacore::AipsError& err) { // lattice region failed
        if (m_xyRegion)
            delete m_xyRegion;
        m_xyRegion = region;
        return false;
    }
    if (m_xyRegion) delete m_xyRegion;
    m_xyRegion = region;
    if (region) xyRegionOK = true;
    return xyRegionOK;
}

casacore::LCRegion* Region::makePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 LCBox
    casacore::LCBox* box(nullptr);
    if (points.size()==1) {
        auto x = points[0].x();
        auto y = points[0].y();
        casacore::IPosition blc(2, x, y), trc(2, x, y);
        box = new casacore::LCBox(blc, trc, m_latticeShape.keepAxes(m_xyAxes));
    }
    return box;
}

casacore::LCRegion* Region::makeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation) {
    casacore::LCPolygon* boxPolygon(nullptr);
    if (points.size()==2) {
        // points are (cx, cy), (width, height)
        float centerX = points[0].x();
        float centerY = points[0].y();
        float width = points[1].x();
        float height = points[1].y();
        casacore::Vector<float> x(4), y(4); // 4 corner points
        if (rotation == 0.0f) {
            float xmin(centerX-width/2.0f), xmax(centerX+width/2.0f);
            float ymin(centerY-height/2.0f), ymax(centerY+height/2.0f);
            // Bottom left
            x(0) = xmin;
            y(0) = ymin;
            // Bottom right
            x(1) = xmax;
            y(1) = ymin;
            // Top right
            x(2) = xmax;
            y(2) = ymax;
            // Top left
            x(3) = xmin;
            y(3) = ymax;
        } else {
            // Apply rotation matrix to get width and height vectors in rotated basis
            float cosX = cos(rotation * M_PI / 180.0f);
            float sinX = sin(rotation * M_PI / 180.0f);
            float widthVectorX = cosX * width;
            float widthVectorY = sinX * width;
            float heightVectorX = -sinX * height;
            float heightVectorY = cosX * height;

            // Bottom left
            x(0) = centerX + (-widthVectorX - heightVectorX) / 2.0f;
            y(0) = centerY + (-widthVectorY - heightVectorY) / 2.0f;
            // Bottom right
            x(1) = centerX + (widthVectorX - heightVectorX) / 2.0f;
            y(1) = centerY + (widthVectorY - heightVectorY) / 2.0f;
            // Top right
            x(2) = centerX + (widthVectorX + heightVectorX) / 2.0f;
            y(2) = centerY + (widthVectorY + heightVectorY) / 2.0f;
            // Top left
            x(3) = centerX + (-widthVectorX + heightVectorX) / 2.0f;
            y(3) = centerY + (-widthVectorY + heightVectorY) / 2.0f;
        }
        boxPolygon = new casacore::LCPolygon(x, y, m_latticeShape.keepAxes(m_xyAxes));
    }
    return boxPolygon;
}

casacore::LCRegion* Region::makeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // LCEllipse from center x,y, bmaj, bmin, rotation
    casacore::LCEllipsoid* ellipse(nullptr);
    if (points.size()==2) {
        float cx(points[0].x()), cy(points[0].y());
        float bmaj(points[1].x()), bmin(points[1].y());
        // rotation is in degrees from y-axis
        // ellipse rotation angle is in radians from x-axis
        float theta = (rotation + 90.0) * (M_PI / 180.0f);
        ellipse = new casacore::LCEllipsoid(cx, cy, bmaj, bmin, theta, m_latticeShape.keepAxes(m_xyAxes));
    }
    return ellipse;
}

casacore::LCRegion* Region::makePolygonRegion(const std::vector<CARTA::Point>& points) {
    // npoints region
    casacore::LCPolygon* polygon(nullptr);
    size_t npoints(points.size());
    casacore::Vector<float> x(npoints), y(npoints);
    for (size_t i=0; i<npoints; ++i) {
        x(i) = points[i].x();
        y(i) = points[i].y();
    }
    polygon = new casacore::LCPolygon(x, y, m_latticeShape.keepAxes(m_xyAxes));
    return polygon;
}

bool Region::makeExtensionBox(casacore::LCBox& extendBox, int stokes, int channel) {
    // Create extension box for stored channel range and given stokes.
    // This can change for different profile/histogram/stats requirements so not stored 
    if (m_latticeShape.size() < 3)
        return false; // not needed

    casacore::IPosition start(m_latticeShape), count(m_latticeShape), boxShape(m_latticeShape);
    if (m_spectralAxis > 0) {
        if (channel == ALL_CHANNELS) {
            start(m_spectralAxis) = 0;
            count(m_spectralAxis) = m_latticeShape(m_spectralAxis);
        } else {
            start(m_spectralAxis) = channel;
            count(m_spectralAxis) = 1;
        }
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

casacore::LCRegion* Region::makeExtendedRegion(int stokes, int channel) {
    // Return 2D lattice region extended by chan range, stokes
    // Returns nullptr if 2D image
    size_t ndim(m_latticeShape.size());
    casacore::LCRegion* region(nullptr);
    if (!isValid() || (ndim==2) || !m_xyRegion)
        return region;

    // create extension box for channel/stokes
    try {
        casacore::LCBox extBox;
        if (!makeExtensionBox(extBox, stokes, channel))
            return region;  // no need for extension
        // specify 0-based extension axes, either axis 2 (3D image) or 2 and 3 (4D image)
        casacore::IPosition extAxes = (ndim == 3 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
        // apply extension box with extension axes to xy region
        region = new casacore::LCExtension(*m_xyRegion, extAxes, extBox);
    } catch (casacore::AipsError& err) {
        // region failed, return nullptr
    }
    return region;
}

casacore::IPosition Region::xyShape() {
    // returns bounding box shape of xy region
    casacore::IPosition xyshape;
    if (m_xyRegion)
        xyshape = m_xyRegion->shape();
    return xyshape;
}

// ***********************************
// Region data

bool Region::getData(std::vector<float>& data, casacore::SubLattice<float>& sublattice) {
    // fill data vector using region SubLattice
    bool dataOK(false);
    casacore::IPosition sublattShape = sublattice.shape();
    if (sublattShape.empty())
        return dataOK;

    data.resize(sublattShape.product());
    casacore::Array<float> tmp(sublattShape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        sublattice.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(sublattShape.size(), 0), sublattShape));
        dataOK = true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }
    return dataOK;
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
    m_stats->fillStatsData(statsData, subLattice, xyRegionValid());
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

std::string Region::getSpatialCoordinate(int profileIndex) {
    return m_profiler->getSpatialCoordinate(profileIndex);
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

std::string Region::getSpectralCoordinate(int profileIndex) {
    return m_profiler->getSpectralCoordinate(profileIndex);
}

void Region::fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex,
    std::vector<float>& spectralData) {
    // Fill SpectralProfile with values for point region;
    // This assumes one spectral config with StatsType::None
    if (isPoint()) {
        CARTA::SetSpectralRequirements_SpectralConfig config;
        if (m_profiler->getSpectralConfig(config, profileIndex)) { // make sure it was requested
            std::string profileCoord(config.coordinate());
            auto newProfile = profileData.add_profiles();
            newProfile->set_coordinate(profileCoord);
            newProfile->set_stats_type(CARTA::StatsType::None);
            *newProfile->mutable_vals() = {spectralData.begin(), spectralData.end()};
        }
    }
}

void Region::fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex,
    casacore::SubLattice<float>& sublattice) {
    // Fill SpectralProfile with statistics values according to config stored in RegionProfiler
    CARTA::SetSpectralRequirements_SpectralConfig config;
    if (m_profiler->getSpectralConfig(config, profileIndex)) {
        std::string profileCoord(config.coordinate());
        const std::vector<int> requestedStats(config.stats_types().begin(), config.stats_types().end());
        size_t nstats = requestedStats.size();
        std::vector<std::vector<double>> statsValues;
        // get values from RegionStats
        if (m_stats->getStatsValues(statsValues, requestedStats, sublattice)) {
            for (size_t i=0; i<nstats; ++i) {
                auto statType = static_cast<CARTA::StatsType>(requestedStats[i]);
                // one SpectralProfile per stats type
                auto newProfile = profileData.add_profiles();
                newProfile->set_coordinate(profileCoord);
                newProfile->set_stats_type(statType);
                // convert to float for spectral profile
                std::vector<float> values(statsValues[i].size());
                for (size_t v=0; v<statsValues[i].size(); ++v) {
                    values[v] = static_cast<float>(statsValues[i][v]);
                }
                *newProfile->mutable_vals() = {values.begin(), values.end()};
            }
        }
    }
}

