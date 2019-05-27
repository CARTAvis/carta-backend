//# Region.h: class for managing a region

#ifndef CARTA_BACKEND_REGION_REGION_H_
#define CARTA_BACKEND_REGION_REGION_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/WCBox.h>

#include <carta-protobuf/spectral_profile.pb.h>

#include "../InterfaceConstants.h"
#include "RegionProfiler.h"
#include "RegionStats.h"

namespace carta {

class Region {
    // Region could be:
    // * the 3D cube for a given stokes
    // * the 2D image for a given channel, stokes
    // * a 1-pixel cursor (point) for a given x, y, and stokes, and all channels
    // * a CARTA::Region type

public:
    Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition image_shape, int spectral_axis, int stokes_axis, const casacore::CoordinateSystem& coord_sys);
    ~Region();

    // to determine if data needs to be updated
    inline bool IsValid() {
        return _valid;
    };
    inline bool IsPoint() {
        return (_type == CARTA::POINT);
    };
    inline bool RegionChanged() {
        return _xy_region_changed;
    };

    // set/get Region parameters
    bool UpdateRegionParameters(
        const std::string name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation);
    inline std::string Name() {
        return _name;
    };
    inline std::vector<CARTA::Point> GetControlPoints() {
        return _control_points;
    };
    casacore::IPosition XyShape();
    const casacore::ArrayLattice<casacore::Bool>* XyMask();
    inline bool XyRegionValid() {
        return (_xy_region != nullptr);
    };

    // get image region for requested stokes and (optionally) single channel
    bool GetRegion(casacore::ImageRegion& region, int stokes, int channel = ALL_CHANNELS);
    // get data from subimage (LCRegion applied to Image by Frame)
    bool GetData(std::vector<float>& data, casacore::ImageInterface<float>& image);

    // Histogram: pass through to RegionStats
    bool SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs);
    CARTA::SetHistogramRequirements_HistogramConfig GetHistogramConfig(int histogram_index);
    size_t NumHistogramConfigs();
    bool GetMinMax(int channel, int stokes, float& min_val, float& max_val);
    void SetMinMax(int channel, int stokes, float min_val, float max_val);
    void CalcMinMax(int channel, int stokes, const std::vector<float>& data, float& min_val, float& max_val);
    bool GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    void SetHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void CalcHistogram(int channel, int stokes, int num_bins, float min_val, float max_val, const std::vector<float>& data,
        CARTA::Histogram& histogram_msg);

    // Spatial: pass through to RegionProfiler
    bool SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes);
    size_t NumSpatialProfiles();
    std::pair<int, int> GetSpatialProfileReq(int profile_index);
    std::string GetSpatialCoordinate(int profile_index);

    // Spectral: pass through to RegionProfiler
    bool SetSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles, const int num_stokes);
    size_t NumSpectralProfiles();
    bool GetSpectralConfigStokes(int& stokes, int profile_index);
    std::string GetSpectralCoordinate(int profile_index);
    bool GetSpectralConfig(CARTA::SetSpectralRequirements_SpectralConfig& config, int profile_index);
    void FillSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index, std::vector<float>& spectral_data);
    void FillSpectralProfileData(
        CARTA::SpectralProfileData& profile_data, int profile_index, std::map<CARTA::StatsType, std::vector<double>>& stats_values);
    void FillSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index, casacore::ImageInterface<float>& image);

    // Stats: pass through to RegionStats
    const std::vector<int>& StatsRequirements() const;
    void SetStatsRequirements(const std::vector<int>& stats_types);
    size_t NumStats();
    void FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes);

private:
    // bounds checking for Region parameters
    bool SetPoints(const std::vector<CARTA::Point>& points);
    bool CheckPoints(const std::vector<CARTA::Point>& points);
    bool CheckPixelPoint(const std::vector<CARTA::Point>& points);
    bool CheckRectanglePoints(const std::vector<CARTA::Point>& points);
    bool CheckEllipsePoints(const std::vector<CARTA::Point>& points);
    bool CheckPolygonPoints(const std::vector<CARTA::Point>& points);
    bool PointsChanged(const std::vector<CARTA::Point>& new_points); // compare new points with stored points

    // Create xy regions
    bool SetXyRegion(const std::vector<CARTA::Point>& points, float rotation); // 2D plane saved as m_xyRegion
    casacore::WCRegion* MakePointRegion(const std::vector<CARTA::Point>& points);
    casacore::WCRegion* MakeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::WCRegion* MakeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::WCRegion* MakePolygonRegion(const std::vector<CARTA::Point>& points);

    // Extend xy region to make LCRegion
    bool MakeExtensionBox(casacore::WCBox& extend_box, int stokes, int channel = ALL_CHANNELS); // for extended region
    casacore::WCRegion* MakeExtendedRegion(int stokes, int channel = ALL_CHANNELS);             // x/y region extended chan/stokes

    // region definition (ICD SET_REGION parameters)
    std::string _name;
    CARTA::RegionType _type;
    std::vector<CARTA::Point> _control_points;
    float _rotation;

    // region flags
    bool _valid, _xy_region_changed;

    // image shape info
    casacore::IPosition _image_shape;
    casacore::IPosition _xy_axes; // first two axes of image shape, to keep or remove
    int _num_dims, _spectral_axis, _stokes_axis;

    // stored 2D region
    casacore::WCRegion* _xy_region;

    // stored 2D mask
    casacore::ArrayLattice<casacore::Bool>* _xy_mask;

    // coordinate system
    casacore::CoordinateSystem _coord_sys;

    // classes for requirements, calculations
    std::unique_ptr<carta::RegionStats> _stats;
    std::unique_ptr<carta::RegionProfiler> _profiler;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGION_H_
