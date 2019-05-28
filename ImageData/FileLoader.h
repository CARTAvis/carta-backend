#ifndef CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FILELOADER_H_

#include <memory>
#include <string>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>

#include <carta-protobuf/enums.pb.h> // StatsType

namespace carta {

namespace FileInfo {

struct ImageStats {
    float min_val;
    float max_val;
    float mean;
    std::vector<float> percentiles;
    std::vector<float> percentile_ranks;
    std::vector<int> histogram_bins;
    int64_t nan_count;
    bool valid;
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
    STATS_2D_MEAN,
    STATS_2D_NANS,
    STATS_2D_HIST,
    STATS_2D_PERCENT,
    STATS_3D,
    STATS_3D_MIN,
    STATS_3D_MAX,
    STATS_3D_MEAN,
    STATS_3D_NANS,
    STATS_3D_HIST,
    STATS_3D_PERCENT,
    // Mask
    MASK
};

inline casacore::ImageOpener::ImageTypes fileType(const std::string& file) {
    return casacore::ImageOpener::imageType(file);
}

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
    // Replaced Lattice with Image Interface - changed back
    using ImageRef = casacore::ImageInterface<float>*;
    using IPos = casacore::IPosition;

    virtual ~FileLoader() = default;

    static FileLoader* GetLoader(const std::string& filename);
    // return coordinates for axis types
    virtual void FindCoords(int& spectral_axis, int& stokes_axis);

    // get shape and axis information from image
    virtual bool FindShape(IPos& shape, size_t& num_channels, size_t& num_stokes, int& spectral_axis, int& stokes_axis);

    // Load image statistics, if they exist, from the file
    virtual void LoadImageStats(bool load_percentiles = false);
    // Retrieve stats for a particular channel or all channels
    virtual FileInfo::ImageStats& GetImageStats(int current_stokes, int channel);

    // Do anything required to open the file (set up cache size, etc)
    virtual void OpenFile(const std::string& hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool HasData(FileInfo::Data ds) const = 0;
    // Return a casacore image type representing the data stored in the
    // specified HDU/group/table/etc.
    virtual ImageRef LoadData(FileInfo::Data ds) = 0;
    virtual bool GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int cursor_y);
    virtual bool GetRegionSpectralData(
        std::map<CARTA::StatsType, std::vector<double>>& data, int stokes, const casacore::ArrayLattice<casacore::Bool>* mask, casacore::IPosition origin, const std::vector<int>& stats_types);
    virtual bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) = 0;

protected:
    virtual bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) = 0;

    // Dimension values used by stats functions
    size_t _num_channels, _num_stokes, _num_dims;
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
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
