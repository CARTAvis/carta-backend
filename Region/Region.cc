//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <algorithm> // max
#include <cmath>     // round

#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCExtension.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/images/Regions/WCRegion.h>

#include "../InterfaceConstants.h"

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, const float rotation,
    const casacore::IPosition imageShape, int spectralAxis, int stokesAxis, const casacore::CoordinateSystem& coordinateSys)
    : m_name(name),
      m_type(type),
      m_rotation(0.0),
      m_valid(false),
      m_xyRegionChanged(false),
      m_imageShape(imageShape),
      m_spectralAxis(spectralAxis),
      m_stokesAxis(stokesAxis),
      m_xyAxes(casacore::IPosition(2, 0, 1)),
      m_xyRegion(nullptr),
      m_coordSys(coordinateSys) {
    // validate and set region parameters
    m_ndim = imageShape.size();
    m_valid = updateRegionParameters(name, type, points, rotation);
    if (m_valid) {
        m_stats = std::unique_ptr<RegionStats>(new RegionStats());
        m_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::~Region() {
    if (m_xyRegion) {
        delete m_xyRegion;
        m_xyRegion = nullptr;
    }
    m_stats.reset();
    m_profiler.reset();
}

// *************************************************************************
// Region settings

bool Region::updateRegionParameters(
    const std::string name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation) {
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
    if (!changed) {                                        // check each point in vectors
        for (size_t i = 0; i < newpoints.size(); ++i) {
            if ((newpoints[i].x() != m_ctrlpoints[i].x()) || (newpoints[i].y() != m_ctrlpoints[i].y())) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

// ***********************************
// Image Region with parameters applied

bool Region::getRegion(casacore::ImageRegion& region, int stokes, int channel) {
    // Return ImageRegion for given stokes and region parameters.
    bool regionOK(false);
    if (!isValid() || (m_xyRegion == nullptr) || (stokes < 0))
        return regionOK;

    casacore::WCRegion* wcregion = makeExtendedRegion(stokes, channel);
    if (wcregion != nullptr) {
        region = casacore::ImageRegion(wcregion);
        regionOK = true;
    }
    return regionOK;
}

bool Region::setXYRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // create 2D casacore::WCRegion for type
    casacore::WCRegion* region(nullptr);
    std::string regionType;
    try {
        switch (m_type) {
            case CARTA::RegionType::POINT: {
                regionType = "POINT";
                region = makePointRegion(points);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                regionType = "RECTANGLE";
                region = makeRectangleRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                regionType = "ELLIPSE";
                region = makeEllipseRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                regionType = "POLYGON";
                region = makePolygonRegion(points);
                break;
            }
            default:
                regionType = "NOT SUPPORTED";
                break;
        }
    } catch (casacore::AipsError& err) { // xy region failed
        std::cerr << "ERROR: xy region type " << regionType << " failed: " << err.getMesg() << std::endl;
    }

    if (m_xyRegion != nullptr)
        delete m_xyRegion;
    m_xyRegion = region;
    return (m_xyRegion != nullptr);
}

casacore::WCRegion* Region::makePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 WCBox
    casacore::WCBox* box(nullptr);
    if (points.size() == 1) {
        auto x = points[0].x();
        auto y = points[0].y();

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int naxes(m_coordSys.nPixelAxes());
        casacore::Vector<casacore::Double> pixelCoords(naxes);
        casacore::Vector<casacore::Double> worldCoords(naxes);
        pixelCoords = 0.0;
        pixelCoords(0) = x;
        pixelCoords(1) = y;
        if (!m_coordSys.toWorld(worldCoords, pixelCoords))
            return box; // nullptr, conversion failed

        // make blc quantities (trc=blc for point)
        casacore::Vector<casacore::String> coordUnits = m_coordSys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(2);
        blc(0) = casacore::Quantity(worldCoords(0), coordUnits(0));
        blc(1) = casacore::Quantity(worldCoords(1), coordUnits(1));
        // using pixel axes 0 and 1
        casacore::IPosition axes(2, 0, 1);
        casacore::Vector<casacore::Int> absRel;
        box = new casacore::WCBox(blc, blc, axes, m_coordSys, absRel);
    }
    return box;
}

casacore::WCRegion* Region::makeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation) {
    casacore::WCPolygon* boxPolygon(nullptr);
    if (points.size() == 2) {
        // points are (cx, cy), (width, height)
        float centerX = points[0].x();
        float centerY = points[0].y();
        float width = points[1].x();
        float height = points[1].y();

        // 4 corner points
        int npoints(4);
        casacore::Vector<casacore::Double> x(npoints), y(npoints);
        if (rotation == 0.0f) {
            float xmin(centerX - width / 2.0f), xmax(centerX + width / 2.0f);
            float ymin(centerY - height / 2.0f), ymax(centerY + height / 2.0f);
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

        // Convert pixel coords to world coords
        int naxes(m_coordSys.nPixelAxes());
        casacore::Matrix<casacore::Double> pixelCoords(naxes, npoints);
        casacore::Matrix<casacore::Double> worldCoords(naxes, npoints);
        pixelCoords = 0.0;
        pixelCoords.row(0) = x;
        pixelCoords.row(1) = y;
        casacore::Vector<casacore::Bool> failures;
        if (!m_coordSys.toWorldMany(worldCoords, pixelCoords, failures))
            return boxPolygon; // nullptr, conversion failed

        // make a vector of quantums for x and y
        casacore::Quantum<casacore::Vector<casacore::Double>> xcoord(worldCoords.row(0));
        casacore::Quantum<casacore::Vector<casacore::Double>> ycoord(worldCoords.row(1));
        casacore::Vector<casacore::String> coordUnits = m_coordSys.worldAxisUnits();
        xcoord.setUnit(coordUnits(0));
        ycoord.setUnit(coordUnits(1));
        // using pixel axes 0 and 1
        casacore::IPosition axes(2, 0, 1);

        boxPolygon = new casacore::WCPolygon(xcoord, ycoord, axes, m_coordSys);
    }
    return boxPolygon;
}

casacore::WCRegion* Region::makeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // WCEllipse from center x,y, bmaj, bmin, rotation
    casacore::WCEllipsoid* ellipse(nullptr);
    if (points.size() == 2) {
        float cx(points[0].x()), cy(points[0].y());
        float bmaj(points[1].x()), bmin(points[1].y());
        // rotation is in degrees from y-axis;
        // ellipse rotation angle is in radians from x-axis
        float theta = (rotation + 90.0) * (M_PI / 180.0f);

        // Convert ellipsoid center pixel coords to world coords
        int naxes(m_coordSys.nPixelAxes());
        casacore::Vector<casacore::Double> pixelCoords(naxes);
        casacore::Vector<casacore::Double> cWorldCoords(naxes);
        pixelCoords = 0.0;
        pixelCoords(0) = cx;
        pixelCoords(1) = cy;
        if (!m_coordSys.toWorld(cWorldCoords, pixelCoords))
            return ellipse; // nullptr, conversion failed

        // make Quantities for ellipsoid center
        casacore::Vector<casacore::String> coordUnits = m_coordSys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> center(2);
        center(0) = casacore::Quantity(cWorldCoords(0), coordUnits(0));
        center(1) = casacore::Quantity(cWorldCoords(1), coordUnits(1));
        // make Quantities for ellipsoid radii
        casacore::Vector<casacore::Quantum<casacore::Double>> radii(2);
        radii(0) = casacore::Quantity(bmaj, "pix");
        radii(1) = casacore::Quantity(bmin, "pix");

        ellipse = new casacore::WCEllipsoid(center, radii, m_xyAxes, m_coordSys);
    }
    return ellipse;
}

casacore::WCRegion* Region::makePolygonRegion(const std::vector<CARTA::Point>& points) {
    // npoints region
    casacore::WCPolygon* polygon(nullptr);
    size_t npoints(points.size());
    casacore::Vector<casacore::Double> x(npoints), y(npoints);
    for (size_t i = 0; i < npoints; ++i) {
        x(i) = points[i].x();
        y(i) = points[i].y();
    }
    casacore::Quantum<casacore::Vector<casacore::Double>> xcoord(x);
    casacore::Quantum<casacore::Vector<casacore::Double>> ycoord(y);
    xcoord.setUnit("pix");
    ycoord.setUnit("pix");
    polygon = new casacore::WCPolygon(xcoord, ycoord, m_xyAxes, m_coordSys);
    return polygon;
}

bool Region::makeExtensionBox(casacore::WCBox& extendBox, int stokes, int channel) {
    // Create extension box for stored channel range and given stokes.
    // This can change for different profile/histogram/stats requirements so not stored
    bool extensionOK(false);
    if (m_ndim < 3)
        return extensionOK; // not needed

    try {
        double minchan(channel), maxchan(channel);
        if (channel == ALL_CHANNELS) { // extend 0 to nchan
            minchan = 0;
            maxchan = m_imageShape(m_spectralAxis);
        }

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int naxes(m_coordSys.nPixelAxes());
        casacore::Vector<casacore::Double> blcPixelCoords(naxes, 0.0);
        casacore::Vector<casacore::Double> trcPixelCoords(naxes, 0.0);
        casacore::Vector<casacore::Double> blcWorldCoords(naxes);
        casacore::Vector<casacore::Double> trcWorldCoords(naxes);
        blcPixelCoords(m_spectralAxis) = minchan;
        trcPixelCoords(m_spectralAxis) = maxchan;
        if (naxes > 3) {
            blcPixelCoords(m_stokesAxis) = stokes;
            trcPixelCoords(m_stokesAxis) = stokes;
        }
        if (!(m_coordSys.toWorld(blcWorldCoords, blcPixelCoords) && m_coordSys.toWorld(trcWorldCoords, trcPixelCoords)))
            return extensionOK; // false, conversions failed

        // make blc, trc Quantities
        int nExtensionAxes(m_ndim - 2);
        casacore::Vector<casacore::String> coordUnits = m_coordSys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(nExtensionAxes);
        casacore::Vector<casacore::Quantum<casacore::Double>> trc(nExtensionAxes);
        // channel quantities
        int chanIndex(m_spectralAxis - 2);
        blc(chanIndex) = casacore::Quantity(blcWorldCoords(m_spectralAxis), coordUnits(m_spectralAxis));
        trc(chanIndex) = casacore::Quantity(trcWorldCoords(m_spectralAxis), coordUnits(m_spectralAxis));
        if (nExtensionAxes > 1) {
            // stokes quantities
            int stokesIndex(m_stokesAxis - 2);
            blc(stokesIndex) = casacore::Quantity(blcWorldCoords(m_stokesAxis), coordUnits(m_stokesAxis));
            trc(stokesIndex) = casacore::Quantity(trcWorldCoords(m_stokesAxis), coordUnits(m_stokesAxis));
        }

        // make extension box
        casacore::IPosition axes = (nExtensionAxes == 1 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
        casacore::Vector<casacore::Int> absRel;
        extendBox = casacore::WCBox(blc, trc, axes, m_coordSys, absRel);
        extensionOK = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Extension box failed: " << err.getMesg() << std::endl;
    }
    return extensionOK;
}

casacore::WCRegion* Region::makeExtendedRegion(int stokes, int channel) {
    // Return 2D wcregion extended by chan, stokes; xyregion if 2D
    if (m_ndim == 2) {
        return m_xyRegion->cloneRegion(); // copy: this ptr owned by ImageRegion
    }

    casacore::WCExtension* region(nullptr);
    try {
        // create extension box for channel/stokes
        casacore::WCBox extBox;
        if (!makeExtensionBox(extBox, stokes, channel))
            return region; // nullptr, extension box failed

        // apply extension box with extension axes to xy region
        region = new casacore::WCExtension(*m_xyRegion, extBox);
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR: Region extension failed: " << err.getMesg() << std::endl;
    }
    return region;
}

casacore::IPosition Region::xyShape() {
    // returns bounding box shape of xy region
    casacore::IPosition xyshape;
    if (m_xyRegion != nullptr) {
        casacore::LCRegion* lcregion = m_xyRegion->toLCRegion(m_coordSys, m_imageShape);
        if (lcregion != nullptr)
            xyshape = lcregion->shape().keepAxes(m_xyAxes);
    }
    return xyshape;
}

// ***********************************
// Region data

bool Region::getData(std::vector<float>& data, casacore::ImageInterface<float>& image) {
    // fill data vector using region masked lattice (subimage)
    bool dataOK(false);
    casacore::IPosition imageShape = image.shape();
    if (imageShape.empty())
        return dataOK;

    data.resize(imageShape.product());
    casacore::Array<float> tmp(imageShape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        image.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(imageShape.size(), 0), imageShape));
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

void Region::calcHistogram(
    int channel, int stokes, int nBins, float minVal, float maxVal, const std::vector<float>& data, CARTA::Histogram& histogramMsg) {
    m_stats->calcHistogram(channel, stokes, nBins, minVal, maxVal, data, histogramMsg);
}

// stats
void Region::setStatsRequirements(const std::vector<int>& statsTypes) {
    m_stats->setStatsRequirements(statsTypes);
}

size_t Region::numStats() {
    return m_stats->numStats();
}

void Region::fillStatsData(CARTA::RegionStatsData& statsData, const casacore::ImageInterface<float>& image, int channel, int stokes) {
    m_stats->fillStatsData(statsData, image, channel, stokes);
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

std::pair<int, int> Region::getSpatialProfileReq(int profileIndex) {
    return m_profiler->getSpatialProfileReq(profileIndex);
}

std::string Region::getSpatialCoordinate(int profileIndex) {
    return m_profiler->getSpatialCoordinate(profileIndex);
}

// spectral

bool Region::setSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs, const int nstokes) {
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

void Region::fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex, std::vector<float>& spectralData) {
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

void Region::fillSpectralProfileData(CARTA::SpectralProfileData& profileData, int profileIndex, casacore::ImageInterface<float>& image) {
    // Fill SpectralProfile with statistics values according to config stored in RegionProfiler
    CARTA::SetSpectralRequirements_SpectralConfig config;
    if (m_profiler->getSpectralConfig(config, profileIndex)) {
        std::string profileCoord(config.coordinate());
        std::vector<int> requestedStats(config.stats_types().begin(), config.stats_types().end());
        size_t nstats = requestedStats.size();
        std::vector<std::vector<double>> statsValues;
        // get values from RegionStats
        bool haveStats(m_stats->calcStatsValues(statsValues, requestedStats, image));
        for (size_t i = 0; i < nstats; ++i) {
            // one SpectralProfile per stats type
            auto newProfile = profileData.add_profiles();
            newProfile->set_coordinate(profileCoord);
            auto statType = static_cast<CARTA::StatsType>(requestedStats[i]);
            newProfile->set_stats_type(statType);
            // convert to float for spectral profile
            std::vector<float> values;
            if (!haveStats || statsValues[i].empty()) { // region outside image or NaNs
                values.resize(1, std::numeric_limits<float>::quiet_NaN());
            } else {
                // convert to float for spectral profile
                values.resize(statsValues[i].size());
                for (size_t v = 0; v < statsValues[i].size(); ++v) {
                    values[v] = static_cast<float>(statsValues[i][v]);
                }
            }
            *newProfile->mutable_vals() = {values.begin(), values.end()};
        }
    }
}
