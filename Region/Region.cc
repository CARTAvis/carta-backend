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
    const casacore::IPosition image_shape, int spectral_axis, int stokes_axis, const casacore::CoordinateSystem& coord_sys)
    : _name(name),
      _type(type),
      _rotation(0.0),
      _valid(false),
      _xy_region_changed(false),
      _image_shape(image_shape),
      _spectral_axis(spectral_axis),
      _stokes_axis(stokes_axis),
      _xy_axes(casacore::IPosition(2, 0, 1)),
      _xy_region(nullptr),
      _coord_sys(coord_sys) {
    // validate and set region parameters
    _num_dims = image_shape.size();
    _valid = UpdateRegionParameters(name, type, points, rotation);
    if (_valid) {
        _stats = std::unique_ptr<RegionStats>(new RegionStats());
        _profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::~Region() {
    if (_xy_region) {
        delete _xy_region;
        _xy_region = nullptr;
    }
    _stats.reset();
    _profiler.reset();
}

// *************************************************************************
// Region settings

bool Region::UpdateRegionParameters(
    const std::string name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation) {
    // Set region parameters and flag if xy region changed
    bool xy_params_changed((PointsChanged(points) || (rotation != _rotation)));

    _name = name;
    _type = type;
    _rotation = rotation;
    // validate and set points, create LCRegion
    bool points_set(SetPoints(points));
    if (points_set)
        SetXyRegion(points, rotation);

    // region changed if xy params changed and points validated
    _xy_region_changed = xy_params_changed && points_set;
    if (_xy_region_changed && _stats)
        _stats->ClearStats(); // recalculate everything

    return points_set;
}

bool Region::SetPoints(const std::vector<CARTA::Point>& points) {
    // check and set control points
    bool points_updated(false);
    if (CheckPoints(points)) {
        _control_points = points;
        points_updated = true;
    }
    return points_updated;
}

// *************************************************************************
// Parameter checking

bool Region::CheckPoints(const std::vector<CARTA::Point>& points) {
    // check point values
    bool points_ok(false);
    switch (_type) {
        case CARTA::POINT: {
            points_ok = CheckPixelPoint(points);
            break;
        }
        case CARTA::RECTANGLE: {
            points_ok = CheckRectanglePoints(points);
            break;
        }
        case CARTA::ELLIPSE: {
            points_ok = CheckEllipsePoints(points);
            break;
        }
        case CARTA::POLYGON: {
            points_ok = CheckPolygonPoints(points);
            break;
        }
        default:
            break;
    }
    return points_ok;
}

bool Region::CheckPixelPoint(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(false);
    if (points.size() == 1) { // (x, y)
        float x(points[0].x()), y(points[0].y());
        points_ok = (std::isfinite(x) && std::isfinite(y));
    }
    return points_ok;
}

bool Region::CheckRectanglePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points, width/height less than 0
    bool points_ok(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), width(points[1].x()), height(points[1].y());
        bool points_exist = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(width) && std::isfinite(height));
        points_ok = (points_exist && (width > 0) && (height > 0));
    }
    return points_ok;
}

bool Region::CheckEllipsePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), bmaj(points[1].x()), bmin(points[1].y());
        points_ok = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(bmaj) && std::isfinite(bmin));
    }
    return points_ok;
}

bool Region::CheckPolygonPoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(true);
    for (auto& point : points)
        points_ok &= (std::isfinite(point.x()) && std::isfinite(point.y()));
    return points_ok;
}

bool Region::PointsChanged(const std::vector<CARTA::Point>& new_points) {
    // check equality of points (no operator==)
    bool changed(new_points.size() != _control_points.size()); // check number of points
    if (!changed) {                                            // check each point in vectors
        for (size_t i = 0; i < new_points.size(); ++i) {
            if ((new_points[i].x() != _control_points[i].x()) || (new_points[i].y() != _control_points[i].y())) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

// ***********************************
// Image Region with parameters applied

bool Region::GetRegion(casacore::ImageRegion& region, int stokes, int channel) {
    // Return ImageRegion for given stokes and region parameters.
    bool region_ok(false);
    if (!IsValid() || (_xy_region == nullptr) || (stokes < 0))
        return region_ok;

    casacore::WCRegion* wc_region = MakeExtendedRegion(stokes, channel);
    if (wc_region != nullptr) {
        region = casacore::ImageRegion(wc_region);
        region_ok = true;
    }
    return region_ok;
}

bool Region::SetXyRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // create 2D casacore::WCRegion for type
    casacore::WCRegion* region(nullptr);
    std::string region_type;
    try {
        switch (_type) {
            case CARTA::RegionType::POINT: {
                region_type = "POINT";
                region = MakePointRegion(points);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                region_type = "RECTANGLE";
                region = MakeRectangleRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                region_type = "ELLIPSE";
                region = MakeEllipseRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                region_type = "POLYGON";
                region = MakePolygonRegion(points);
                break;
            }
            default:
                region_type = "NOT SUPPORTED";
                break;
        }
    } catch (casacore::AipsError& err) { // xy region failed
        std::cerr << "ERROR: xy region type " << region_type << " failed: " << err.getMesg() << std::endl;
    }

    delete _xy_region;
    _xy_region = region;
    return (_xy_region != nullptr);
}

casacore::WCRegion* Region::MakePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 WCBox
    casacore::WCBox* box(nullptr);
    if (points.size() == 1) {
        auto x = points[0].x();
        auto y = points[0].y();

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int naxes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> pixel_coords(naxes);
        casacore::Vector<casacore::Double> world_coords(naxes);
        pixel_coords = 0.0;
        pixel_coords(0) = x;
        pixel_coords(1) = y;
        if (!_coord_sys.toWorld(world_coords, pixel_coords))
            return box; // nullptr, conversion failed

        // make blc quantities (trc=blc for point)
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(2);
        blc(0) = casacore::Quantity(world_coords(0), coord_units(0));
        blc(1) = casacore::Quantity(world_coords(1), coord_units(1));
        // using pixel axes 0 and 1
        casacore::IPosition axes(2, 0, 1);
        casacore::Vector<casacore::Int> abs_rel;
        box = new casacore::WCBox(blc, blc, axes, _coord_sys, abs_rel);
    }
    return box;
}

casacore::WCRegion* Region::MakeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation) {
    casacore::WCPolygon* box_polygon(nullptr);
    if (points.size() == 2) {
        // points are (cx, cy), (width, height)
        float center_x = points[0].x();
        float center_y = points[0].y();
        float width = points[1].x();
        float height = points[1].y();

        // 4 corner points
        int num_points(4);
        casacore::Vector<casacore::Double> x(num_points), y(num_points);
        if (rotation == 0.0f) {
            float x_min(center_x - width / 2.0f), x_max(center_x + width / 2.0f);
            float y_min(center_y - height / 2.0f), y_max(center_y + height / 2.0f);
            // Bottom left
            x(0) = x_min;
            y(0) = y_min;
            // Bottom right
            x(1) = x_max;
            y(1) = y_min;
            // Top right
            x(2) = x_max;
            y(2) = y_max;
            // Top left
            x(3) = x_min;
            y(3) = y_max;
        } else {
            // Apply rotation matrix to get width and height vectors in rotated basis
            float cos_x = cos(rotation * M_PI / 180.0f);
            float sin_x = sin(rotation * M_PI / 180.0f);
            float width_vector_x = cos_x * width;
            float width_vector_y = sin_x * width;
            float height_vector_x = -sin_x * height;
            float height_vector_y = cos_x * height;

            // Bottom left
            x(0) = center_x + (-width_vector_x - height_vector_x) / 2.0f;
            y(0) = center_y + (-width_vector_y - height_vector_y) / 2.0f;
            // Bottom right
            x(1) = center_x + (width_vector_x - height_vector_x) / 2.0f;
            y(1) = center_y + (width_vector_y - height_vector_y) / 2.0f;
            // Top right
            x(2) = center_x + (width_vector_x + height_vector_x) / 2.0f;
            y(2) = center_y + (width_vector_y + height_vector_y) / 2.0f;
            // Top left
            x(3) = center_x + (-width_vector_x + height_vector_x) / 2.0f;
            y(3) = center_y + (-width_vector_y + height_vector_y) / 2.0f;
        }

        // Convert pixel coords to world coords
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
        casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
        pixel_coords = 0.0;
        pixel_coords.row(0) = x;
        pixel_coords.row(1) = y;
        casacore::Vector<casacore::Bool> failures;
        if (!_coord_sys.toWorldMany(world_coords, pixel_coords, failures))
            return box_polygon; // nullptr, conversion failed

        // make a vector of quantums for x and y
        casacore::Quantum<casacore::Vector<casacore::Double>> x_coord(world_coords.row(0));
        casacore::Quantum<casacore::Vector<casacore::Double>> y_coord(world_coords.row(1));
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        x_coord.setUnit(coord_units(0));
        y_coord.setUnit(coord_units(1));
        // using pixel axes 0 and 1
        casacore::IPosition axes(2, 0, 1);

        box_polygon = new casacore::WCPolygon(x_coord, y_coord, axes, _coord_sys);
    }
    return box_polygon;
}

casacore::WCRegion* Region::MakeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // WCEllipse from center x,y, bmaj, bmin, rotation
    casacore::WCEllipsoid* ellipse(nullptr);
    
    if (points.size() == 2) {
        float cx(points[0].x()), cy(points[0].y());
        float bmaj(points[1].x()), bmin(points[1].y());
        // rotation is in degrees from y-axis;
        // ellipse rotation angle is in radians from x-axis
        float theta = (rotation + 90.0) * (M_PI / 180.0f);

        // Convert ellipsoid center pixel coords to world coords
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> pixel_coords(num_axes);
        casacore::Vector<casacore::Double> world_coords(num_axes);
        pixel_coords = 0.0;
        pixel_coords(0) = cx;
        pixel_coords(1) = cy;
        if (!_coord_sys.toWorld(world_coords, pixel_coords))
            return ellipse; // nullptr, conversion failed

        // make Quantities for ellipsoid center
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> center(2);
        center(0) = casacore::Quantity(world_coords(0), coord_units(0));
        center(1) = casacore::Quantity(world_coords(1), coord_units(1));
	
        // make Quantities for ellipsoid radii
        casacore::Vector<casacore::Quantum<casacore::Double>> radii(2);
        radii(0) = casacore::Quantity(bmaj, "pix");
        radii(1) = casacore::Quantity(bmin, "pix");
	
	double doubleTheta;
	doubleTheta = static_cast<double>(theta);
	
	casacore::Quantity quantityTheta = casacore::Quantity(doubleTheta, "rad");
	
	// Make sure the major axis is greater than the minor axis
	casacore::Quantity majorAxis;
	casacore::Quantity minorAxis;
	if (radii(0) < radii(1)) {
	    majorAxis = radii(1);
	    minorAxis = radii(0);
	  } else {
	    majorAxis = radii(0);
	    minorAxis = radii(1);
	  }
	ellipse = new casacore::WCEllipsoid(center(0), center(1), majorAxis, minorAxis, quantityTheta, _xy_axes(0), _xy_axes(1), _coord_sys);
    }
    return ellipse;
}

casacore::WCRegion* Region::MakePolygonRegion(const std::vector<CARTA::Point>& points) {
    // npoints region
    casacore::WCPolygon* polygon(nullptr);
    size_t num_points(points.size());
    casacore::Vector<casacore::Double> x(num_points), y(num_points);
    for (size_t i = 0; i < num_points; ++i) {
        x(i) = points[i].x();
        y(i) = points[i].y();
    }
    casacore::Quantum<casacore::Vector<casacore::Double>> x_coord(x);
    casacore::Quantum<casacore::Vector<casacore::Double>> y_coord(y);
    x_coord.setUnit("pix");
    y_coord.setUnit("pix");
    polygon = new casacore::WCPolygon(x_coord, y_coord, _xy_axes, _coord_sys);
    return polygon;
}

bool Region::MakeExtensionBox(casacore::WCBox& extend_box, int stokes, int channel) {
    // Create extension box for stored channel range and given stokes.
    // This can change for different profile/histogram/stats requirements so not stored
    bool extension_ok(false);
    if (_num_dims < 3)
        return extension_ok; // not needed

    try {
        double min_chan(channel), max_chan(channel);
        if (channel == ALL_CHANNELS) { // extend 0 to nchan
            min_chan = 0;
            max_chan = _image_shape(_spectral_axis);
        }

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> blc_pixel_coords(num_axes, 0.0);
        casacore::Vector<casacore::Double> trc_pixel_coords(num_axes, 0.0);
        casacore::Vector<casacore::Double> blc_world_coords(num_axes);
        casacore::Vector<casacore::Double> trc_world_coords(num_axes);
        blc_pixel_coords(_spectral_axis) = min_chan;
        trc_pixel_coords(_spectral_axis) = max_chan;
        if (num_axes > 3) {
            blc_pixel_coords(_stokes_axis) = stokes;
            trc_pixel_coords(_stokes_axis) = stokes;
        }
        if (!(_coord_sys.toWorld(blc_world_coords, blc_pixel_coords) && _coord_sys.toWorld(trc_world_coords, trc_pixel_coords)))
            return extension_ok; // false, conversions failed

        // make blc, trc Quantities
        int num_extension_axes(_num_dims - 2);
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(num_extension_axes);
        casacore::Vector<casacore::Quantum<casacore::Double>> trc(num_extension_axes);
        // channel quantities
        int chan_index(_spectral_axis - 2);
        blc(chan_index) = casacore::Quantity(blc_world_coords(_spectral_axis), coord_units(_spectral_axis));
        trc(chan_index) = casacore::Quantity(trc_world_coords(_spectral_axis), coord_units(_spectral_axis));
        if (num_extension_axes > 1) {
            // stokes quantities
            int stokes_index(_stokes_axis - 2);
            blc(stokes_index) = casacore::Quantity(blc_world_coords(_stokes_axis), coord_units(_stokes_axis));
            trc(stokes_index) = casacore::Quantity(trc_world_coords(_stokes_axis), coord_units(_stokes_axis));
        }

        // make extension box
        casacore::IPosition axes = (num_extension_axes == 1 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
        casacore::Vector<casacore::Int> abs_rel;
        extend_box = casacore::WCBox(blc, trc, axes, _coord_sys, abs_rel);
        extension_ok = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Extension box failed: " << err.getMesg() << std::endl;
    }
    return extension_ok;
}

casacore::WCRegion* Region::MakeExtendedRegion(int stokes, int channel) {
    // Return 2D wcregion extended by chan, stokes; xyregion if 2D
    if (_num_dims == 2) {
        return _xy_region->cloneRegion(); // copy: this ptr owned by ImageRegion
    }

    casacore::WCExtension* region(nullptr);
    try {
        // create extension box for channel/stokes
        casacore::WCBox ext_box;
        if (!MakeExtensionBox(ext_box, stokes, channel))
            return region; // nullptr, extension box failed

        // apply extension box with extension axes to xy region
        region = new casacore::WCExtension(*_xy_region, ext_box);
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR: Region extension failed: " << err.getMesg() << std::endl;
    }
    return region;
}

casacore::IPosition Region::XyShape() {
    // returns bounding box shape of xy region
    casacore::IPosition xy_shape;
    if (_xy_region != nullptr) {
        casacore::LCRegion* region = _xy_region->toLCRegion(_coord_sys, _image_shape);
        if (region != nullptr)
            xy_shape = region->shape().keepAxes(_xy_axes);
    }
    return xy_shape;
}

// ***********************************
// Region data

bool Region::GetData(std::vector<float>& data, casacore::ImageInterface<float>& image) {
    // fill data vector using region masked lattice (subimage)
    bool data_ok(false);
    casacore::IPosition image_shape = image.shape();
    if (image_shape.empty())
        return data_ok;

    data.resize(image_shape.product());
    casacore::Array<float> tmp(image_shape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        image.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(image_shape.size(), 0), image_shape));
        data_ok = true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }
    return data_ok;
}

// ***********************************
// RegionStats

// histogram

bool Region::SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs) {
    _stats->SetHistogramRequirements(histogram_reqs);
    return true;
}

CARTA::SetHistogramRequirements_HistogramConfig Region::GetHistogramConfig(int histogram_index) {
    return _stats->GetHistogramConfig(histogram_index);
}

size_t Region::NumHistogramConfigs() {
    return _stats->NumHistogramConfigs();
}

bool Region::GetMinMax(int channel, int stokes, float& min_val, float& max_val) {
    return _stats->GetMinMax(channel, stokes, min_val, max_val);
}

void Region::SetMinMax(int channel, int stokes, float min_val, float max_val) {
    std::pair<float, float> vals = std::make_pair(min_val, max_val);
    _stats->SetMinMax(channel, stokes, vals);
}

void Region::CalcMinMax(int channel, int stokes, const std::vector<float>& data, float& min_val, float& max_val) {
    _stats->CalcMinMax(channel, stokes, data, min_val, max_val);
}

bool Region::GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram) {
    return _stats->GetHistogram(channel, stokes, num_bins, histogram);
}

void Region::SetHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    _stats->SetHistogram(channel, stokes, histogram);
}

void Region::CalcHistogram(
    int channel, int stokes, int num_bins, float min_val, float max_val, const std::vector<float>& data, CARTA::Histogram& histogram_msg) {
    _stats->CalcHistogram(channel, stokes, num_bins, min_val, max_val, data, histogram_msg);
}

// stats
void Region::SetStatsRequirements(const std::vector<int>& stats_types) {
    _stats->SetStatsRequirements(stats_types);
}

size_t Region::NumStats() {
    return _stats->NumStats();
}

void Region::FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes) {
    _stats->FillStatsData(stats_data, image, channel, stokes);
}

// ***********************************
// RegionProfiler

// spatial

bool Region::SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes) {
    return _profiler->SetSpatialRequirements(profiles, num_stokes);
}

size_t Region::NumSpatialProfiles() {
    return _profiler->NumSpatialProfiles();
}

std::pair<int, int> Region::GetSpatialProfileReq(int profile_index) {
    return _profiler->GetSpatialProfileReq(profile_index);
}

std::string Region::GetSpatialCoordinate(int profile_index) {
    return _profiler->GetSpatialCoordinate(profile_index);
}

// spectral

bool Region::SetSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs, const int num_stokes) {
    return _profiler->SetSpectralRequirements(configs, num_stokes);
}

size_t Region::NumSpectralProfiles() {
    return _profiler->NumSpectralProfiles();
}

bool Region::GetSpectralConfigStokes(int& stokes, int profile_index) {
    return _profiler->GetSpectralConfigStokes(stokes, profile_index);
}

bool Region::GetSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profile_index) {
    return _profiler->GetSpectralConfig(config, profile_index);
}

std::string Region::GetSpectralCoordinate(int profile_index) {
    return _profiler->GetSpectralCoordinate(profile_index);
}

void Region::FillSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index, std::vector<float>& spectral_data) {
    // Fill SpectralProfile with values for point region;
    // This assumes one spectral config with StatsType::Sum
    if (IsPoint()) {
        CARTA::SetSpectralRequirements_SpectralConfig config;
        if (_profiler->GetSpectralConfig(config, profile_index)) { // make sure it was requested
            std::string profile_coord(config.coordinate());
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            new_profile->set_stats_type(CARTA::StatsType::Sum);
            *new_profile->mutable_vals() = {spectral_data.begin(), spectral_data.end()};
        }
    }
}

void Region::FillSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index, casacore::ImageInterface<float>& image) {
    // Fill SpectralProfile with statistics values according to config stored in RegionProfiler
    CARTA::SetSpectralRequirements_SpectralConfig config;
    if (_profiler->GetSpectralConfig(config, profile_index)) {
        std::string profile_coord(config.coordinate());
        std::vector<int> requested_stats(config.stats_types().begin(), config.stats_types().end());
        size_t nstats = requested_stats.size();
        std::vector<std::vector<double>> stats_values;
        // get values from RegionStats
        bool have_stats(_stats->CalcStatsValues(stats_values, requested_stats, image));
        for (size_t i = 0; i < nstats; ++i) {
            // one SpectralProfile per stats type
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            auto stat_type = static_cast<CARTA::StatsType>(requested_stats[i]);
            new_profile->set_stats_type(stat_type);
            // convert to float for spectral profile
            std::vector<float> values;
            if (!have_stats || stats_values[i].empty()) { // region outside image or NaNs
                new_profile->add_double_vals(std::numeric_limits<float>::quiet_NaN());
            } else {
                *new_profile->mutable_double_vals() = {stats_values[i].begin(), stats_values[i].end()};
            }
        }
    }
}
