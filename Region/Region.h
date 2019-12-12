//# Region.h: class for managing a region

#ifndef CARTA_BACKEND_REGION_REGION_H_
#define CARTA_BACKEND_REGION_REGION_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/WCBox.h>
#include <imageanalysis/Annotations/AnnRegion.h>

#include <carta-protobuf/spectral_profile.pb.h>

#include <tbb/atomic.h>
#include <thread>

#include "../InterfaceConstants.h"
#include "../Util.h"
#include "RegionProfiler.h"
#include "RegionStats.h"

namespace carta {

struct StatsCacheData {
    std::vector<double> stats_values; // Spectral profile
    size_t channel_end;               // End of the channel index from previous calculation
};

class Region {
    // Region could be:
    // * the 3D cube for a given stokes
    // * the 2D image for a given channel, stokes
    // * a 1-pixel cursor (point) for a given x, y, and stokes, and all channels
    // * a CARTA::Region type

public:
    // Constructors
    // Region created from SET_REGION
    Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, const float rotation,
        const casacore::IPosition image_shape, int spectral_axis, int stokes_axis, const casacore::CoordinateSystem& coord_sys);
    // Region created from CRTF file
    Region(casacore::CountedPtr<const casa::AnnotationBase> annotation_region, const casacore::IPosition image_shape, int spectral_axis,
        int stokes_axis, const casacore::CoordinateSystem& coord_sys);

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
    inline CARTA::RegionType Type() {
        return _type;
    };
    inline std::vector<CARTA::Point> GetControlPoints() {
        return _control_points;
    };
    inline std::vector<casacore::Quantity> GetControlPointsWcs() {
        return _control_points_wcs;
    }

    inline float Rotation() {
        return _rotation;
    };

    casacore::IPosition XyShape();
    casacore::IPosition XyOrigin();
    std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> XyMask();
    inline bool XyRegionValid() {
        return bool(_xy_region);
    };
    casacore::CountedPtr<const casa::AnnotationBase> AnnotationRegion(bool pixel_coord = true);

    // get image region for requested stokes and (optionally) single channel
    bool GetRegion(casacore::ImageRegion& region, int stokes, ChannelRange channel_range = {0, ALL_CHANNELS});
    // get data from subimage (LCRegion applied to Image by Frame)
    bool GetData(std::vector<float>& data, casacore::ImageInterface<float>& image);

    // Histogram: pass through to RegionStats
    bool SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs);
    CARTA::SetHistogramRequirements_HistogramConfig GetHistogramConfig(int histogram_index);
    size_t NumHistogramConfigs();
    bool GetBasicStats(int channel, int stokes, BasicStats<float>& stats);
    void SetBasicStats(int channel, int stokes, const BasicStats<float>& stats);
    void CalcBasicStats(int channel, int stokes, const std::vector<float>& data, BasicStats<float>& stats);
    bool GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    void SetHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void CalcHistogram(int channel, int stokes, int num_bins, const BasicStats<float>& stats, const std::vector<float>& data,
        CARTA::Histogram& histogram_msg);

    void SetAllProfilesUnsent(); // enable sending new spatial and spectral profiles

    // Spatial requirements: pass through to RegionProfiler
    bool SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes);
    size_t NumSpatialProfiles();
    std::string GetSpatialCoordinate(int profile_index);
    std::pair<int, int> GetSpatialProfileAxes(int profile_index);
    bool GetSpatialProfileSent(int profile_index);
    void SetSpatialProfileSent(int profile_index, bool sent);

    // Spectral requirements: pass through to RegionProfiler
    bool SetSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles, const int num_stokes);
    size_t NumSpectralProfiles();
    bool IsValidSpectralConfig(const SpectralConfig& stats);
    std::vector<SpectralProfile> GetSpectralProfiles();
    bool GetSpectralConfig(int config_stokes, SpectralConfig& config);
    bool GetSpectralProfileAllStatsSent(int config_stokes);
    void SetSpectralProfileAllStatsSent(int config_stokes, bool sent);
    void SetAllSpectralProfilesUnsent();

    // Spectral data
    bool GetSpectralProfileData(std::map<CARTA::StatsType, std::vector<double>>& spectral_data, casacore::ImageInterface<float>& image);
    void FillPointSpectralProfileDataMessage(
        CARTA::SpectralProfileData& profile_message, int config_stokes, std::vector<float>& spectral_data);
    void FillSpectralProfileDataMessage(
        CARTA::SpectralProfileData& profile_message, int config_stokes, std::map<CARTA::StatsType, std::vector<double>>& spectral_data);
    void FillNaNSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, int config_stokes);
    void InitSpectralData(
        int profile_stokes, size_t profile_size, std::map<CARTA::StatsType, std::vector<double>>& stats_data, size_t& channel_start);

    // Stats: pass through to RegionStats
    void SetStatsRequirements(const std::vector<int>& stats_types);
    size_t NumStats();
    void FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes);
    void FillStatsData(CARTA::RegionStatsData& stats_data, std::map<CARTA::StatsType, double>& stats_values);
    void FillNaNStatsData(CARTA::RegionStatsData& stats_data);

    // Get current region state
    RegionState GetRegionState();

    // Communication
    bool IsConnected();
    void DisconnectCalled();

    void IncreaseZProfileCount() {
        ++_z_profile_count;
    }
    void DecreaseZProfileCount() {
        --_z_profile_count;
    }

    // Set stats cache
    void SetStatsCache(int profile_stokes, std::map<CARTA::StatsType, std::vector<double>>& stats_data, size_t channel_end);
    // Get stats cache
    bool GetStatsCache(
        int profile_stokes, size_t profile_size, CARTA::StatsType stats_type, std::vector<double>& stats_data, size_t& channel_start);

private:
    // bounds checking for Region parameters
    bool SetPoints(const std::vector<CARTA::Point>& points);
    bool CheckPoints(const std::vector<CARTA::Point>& points);
    bool CheckPixelPoint(const std::vector<CARTA::Point>& points);
    bool CheckRectanglePoints(const std::vector<CARTA::Point>& points);
    bool CheckEllipsePoints(const std::vector<CARTA::Point>& points);
    bool CheckPolygonPoints(const std::vector<CARTA::Point>& points);
    bool PointsChanged(const std::vector<CARTA::Point>& new_points); // compare new points with stored points

    // conversion between world and pixel coordinates
    double AngleToLength(casacore::Quantity angle, const unsigned int pixel_axis);
    bool CartaPointToWorld(const CARTA::Point& point, casacore::Vector<casacore::Quantity>& world_point);
    bool XyPixelsToWorld(casacore::Vector<casacore::Double> x, casacore::Vector<casacore::Double> y,
        casacore::Quantum<casacore::Vector<casacore::Double>>& x_world, casacore::Quantum<casacore::Vector<casacore::Double>>& y_world);

    // Imported rectangles are polygon vertices; calculate center point, width, and height
    void GetRectangleControlPointsFromVertices(
        std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, double& cx, double& cy, double& width, double& height);
    void GetRectangleControlPointsFromVertices(std::vector<casacore::Quantity>& x, std::vector<casacore::Quantity>& y,
        casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width, casacore::Quantity& height);

    // For region export, need stokes types from coordinate system
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();

    // Create xy regions
    bool SetXyRegion(const std::vector<CARTA::Point>& points, float rotation); // 2D plane saved as m_xyRegion
    casacore::WCRegion* MakePointRegion(const std::vector<CARTA::Point>& points);
    casacore::WCRegion* MakeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::WCRegion* MakeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation);
    casacore::WCRegion* MakePolygonRegion(const std::vector<CARTA::Point>& points);
    // Creation of casacore regions is not threadsafe
    std::mutex _casacore_region_mutex;

    // Extend xy region to make LCRegion
    bool MakeExtensionBox(casacore::WCBox& extend_box, int stokes, ChannelRange channel_range); // for extended region
    casacore::WCRegion* MakeExtendedRegion(int stokes, ChannelRange channel_range);             // x/y region extended chan/stokes

    // internal for spectral profile message
    std::string GetSpectralCoordinate(int profile_index);
    bool GetSpectralStatsToLoad(int profile_index, std::vector<int>& stats);
    bool GetSpectralProfileStatSent(int profile_index, int stats_type);

    void SetConnectionFlag(bool connected);

    // Reset stats cache
    void ResetStatsCache();

    // region definition (ICD SET_REGION parameters)
    std::string _name;
    CARTA::RegionType _type;
    std::vector<CARTA::Point> _control_points;
    std::vector<casacore::Quantity> _control_points_wcs;
    float _rotation;

    // region flags
    bool _valid, _xy_region_changed;

    // image shape info
    casacore::IPosition _image_shape;
    casacore::IPosition _xy_axes; // first two axes of image shape, to keep or remove
    int _num_dims, _spectral_axis, _stokes_axis;

    // stored 2D region
    std::shared_ptr<const casacore::WCRegion> _xy_region;

    // stored 2D mask
    std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> _xy_mask;

    // coordinate system
    casacore::CoordinateSystem _coord_sys;

    // classes for requirements, calculations
    std::unique_ptr<carta::RegionStats> _region_stats;
    std::unique_ptr<carta::RegionProfiler> _region_profiler;

    // Communication
    volatile bool _connected = true;

    // Spectral profile counter, which is used to determine whether the Region object can be destroyed (_z_profile_count == 0 ?).
    tbb::atomic<int> _z_profile_count;

    // Map of stats cache: "stoke index" vs. {"stats type" vs. ["stats values", "channel end"]}
    std::map<int, std::map<CARTA::StatsType, StatsCacheData>> _stats_cache;

    // Lock when stats cache is being read or written
    std::mutex _stats_cache_mutex;

    // Define all stats types to calculate
    std::vector<int> _all_stats = {CARTA::StatsType::Sum, CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma,
        CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max};
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGION_H_
