#ifndef CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FILELOADER_H_

#include <memory>
#include <string>

#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/enums.pb.h>

#include "../Util.h"

class Frame;

namespace carta {

namespace FileInfo {

struct ImageStats {
    std::map<CARTA::StatsType, double> basic_stats;

    std::vector<float> percentiles;
    std::vector<float> percentile_ranks;
    std::vector<int> histogram_bins;

    bool valid;
    // Remove this check when we drop support for the old schema.
    bool full;
};

struct RegionStatsId {
    int region_id;
    int stokes;

    RegionStatsId() {}

    RegionStatsId(int region_id, int stokes) : region_id(region_id), stokes(stokes) {}

    bool operator<(const RegionStatsId& rhs) const {
        return (region_id < rhs.region_id) || ((region_id == rhs.region_id) && (stokes < rhs.stokes));
    }
};

struct RegionSpectralStats {
    casacore::IPosition origin;
    casacore::IPosition shape;
    std::map<CARTA::StatsType, std::vector<double>> stats;
    volatile bool completed = false;
    size_t latest_x = 0;

    RegionSpectralStats() {}

    RegionSpectralStats(casacore::IPosition origin, casacore::IPosition shape, int num_channels) : origin(origin), shape(shape) {
        std::vector<CARTA::StatsType> supported_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount, CARTA::StatsType::Sum,
            CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min,
            CARTA::StatsType::Max};

        for (auto& s : supported_stats) {
            stats.emplace(std::piecewise_construct, std::make_tuple(s), std::make_tuple(num_channels));
        }
    }

    bool IsValid(casacore::IPosition origin, casacore::IPosition shape) {
        return (origin.isEqual(this->origin) && shape.isEqual(this->shape));
    }
    bool IsCompleted() {
        return completed;
    }
};

enum class Data : uint32_t {
    // Main dataset
    Image,
    // Possible aliases to main dataset
    XY,
    XYZ,
    XYZW,
    // Possible swizzled datasets
    YX,
    ZYX,
    ZYXW,
    // Alias to swizzled dataset
    SWIZZLED,
    // Statistics tables
    STATS,
    RANKS,
    STATS_2D,
    STATS_2D_MIN,
    STATS_2D_MAX,
    STATS_2D_SUM,
    STATS_2D_SUMSQ,
    STATS_2D_NANS,
    STATS_2D_HIST,
    STATS_2D_PERCENT,
    STATS_3D,
    STATS_3D_MIN,
    STATS_3D_MAX,
    STATS_3D_SUM,
    STATS_3D_SUMSQ,
    STATS_3D_NANS,
    STATS_3D_HIST,
    STATS_3D_PERCENT,
    // Mask
    MASK
};

inline casacore::uInt GetFitsHdu(const std::string& hdu) {
    // convert from string to casacore unsigned int
    casacore::uInt hdu_num(0);
    if (!hdu.empty() && hdu != "0") {
        casacore::String cc_hdu(hdu);
        cc_hdu.fromString(hdu_num, true);
    }
    return hdu_num;
}

} // namespace FileInfo

class FileLoader {
public:
    using ImageRef = casacore::ImageInterface<float>*;
    using IPos = casacore::IPosition;

    virtual ~FileLoader() = default;

    static FileLoader* GetLoader(const std::string& filename);

    // check for mirlib (MIRIAD) error; returns true for other image types
    virtual bool CanOpenFile(std::string& error);
    // Do anything required to open the file at given HDU (set up cache size, Image object)
    virtual void OpenFile(const std::string& hdu) = 0;

    // Return the opened casacore image
    virtual ImageRef GetImage() = 0;

    // Image shape and coordinate system axes
    bool GetShape(IPos& shape);
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys);
    bool FindCoordinateAxes(IPos& shape, int& spectral_axis, int& stokes_axis, std::string& message);

    // Image Data
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool HasData(FileInfo::Data ds) const = 0;
    // Slice image data (with mask applied)
    bool GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer, bool removeDegenerateAxes = false);

    // Image Statistics
    // Load image statistics, if they exist, from the file
    virtual void LoadImageStats(bool load_percentiles = false);
    // Retrieve stats for a particular channel or all channels
    virtual FileInfo::ImageStats& GetImageStats(int current_stokes, int channel);

    // Spectral profiles
    virtual bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex);
    // check if one can apply swizzled data under such image format and region condition
    virtual bool UseRegionSpectralData(const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, std::mutex& image_mutex);
    virtual bool GetRegionSpectralData(int region_id, int config_stokes, int profile_stokes,
        const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, IPos origin, std::mutex& image_mutex,
        const std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)>& partial_results_callback);
    // Implemented in Hdf5Loader, used to interrupt loading spectral profile
    virtual void SetFramePtr(Frame* frame);

protected:
    // Dimension values used by stats functions
    size_t _num_channels, _num_stokes, _num_dims, _channel_size;

    // Storage for channel and cube statistics
    std::vector<std::vector<carta::FileInfo::ImageStats>> _channel_stats;
    std::vector<carta::FileInfo::ImageStats> _cube_stats;

    // Return the shape of the specified stats dataset
    virtual const IPos GetStatsDataShape(FileInfo::Data ds);

    // Return stats data as a casacore::Array of type casacore::Float or casacore::Int64
    virtual casacore::ArrayBase* GetStatsData(FileInfo::Data ds);

    // Functions for loading individual types of statistics
    virtual void LoadStats2DBasic(FileInfo::Data ds);
    virtual void LoadStats2DHist();
    virtual void LoadStats2DPercent();
    virtual void LoadStats3DBasic(FileInfo::Data ds);
    virtual void LoadStats3DHist();
    virtual void LoadStats3DPercent();

private:
    // Find spectral and stokes coordinates
    void FindCoordinates(int& spectral_axis, int& stokes_axis);
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
