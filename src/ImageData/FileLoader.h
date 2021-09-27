/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FILELOADER_H_

#include <memory>
#include <string>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

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

    RegionSpectralStats(casacore::IPosition origin, casacore::IPosition shape, int num_channels, bool has_flux = false)
        : origin(origin), shape(shape) {
        std::vector<CARTA::StatsType> supported_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount, CARTA::StatsType::Sum,
            CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min,
            CARTA::StatsType::Max, CARTA::StatsType::Extrema};

        if (has_flux) {
            supported_stats.push_back(CARTA::StatsType::FluxDensity);
        }

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
    using ImageRef = std::shared_ptr<casacore::ImageInterface<float>>;
    using IPos = casacore::IPosition;

    FileLoader(const std::string& filename, bool is_gz = false);
    virtual ~FileLoader() = default;

    static FileLoader* GetLoader(const std::string& filename);
    // Access an image from the memory, not from the disk
    static FileLoader* GetLoader(std::shared_ptr<casacore::ImageInterface<float>> image);

    // check for mirlib (MIRIAD) error; returns true for other image types
    virtual bool CanOpenFile(std::string& error);
    // Open and close file
    virtual void OpenFile(const std::string& hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool HasData(FileInfo::Data ds) const;

    // If not in use, temp close image to prevent caching
    void CloseImageIfUpdated();

    // Return the opened casacore image or its class name
    ImageRef GetImage();

    // read beam subtable
    bool GetBeams(std::vector<CARTA::Beam>& beams, std::string& error);

    // Image shape and coordinate system axes
    bool GetShape(IPos& shape);
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys);
    bool FindCoordinateAxes(IPos& shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message);
    std::vector<int> GetRenderAxes(); // Determine axes used for image raster data

    // Slice image data (with mask applied)
    bool GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer);

    // SubImage
    bool GetSubImage(const casacore::Slicer& slicer, casacore::SubImage<float>& sub_image);
    bool GetSubImage(const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image);
    bool GetSubImage(const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image);

    // Image Statistics
    // Load image statistics, if they exist, from the file
    virtual void LoadImageStats(bool load_percentiles = false);
    // Retrieve stats for a particular channel or all channels
    virtual FileInfo::ImageStats& GetImageStats(int current_stokes, int channel);

    // Spectral profiles for cursor and region
    virtual bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex);
    // Check if one can apply swizzled data under such image format and region condition
    virtual bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex);
    virtual bool GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress);
    virtual bool GetDownsampledRasterData(
        std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex);
    virtual bool GetChunk(
        std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex);

    virtual bool HasMip(int mip) const;
    virtual bool UseTileCache() const;

    // Get the full name of image file
    std::string GetFileName();

    // Handle stokes type index
    virtual void SetFirstStokesType(int stokes_value);
    virtual void SetDeltaStokesIndex(int delta_stokes_index);
    virtual bool GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index);

protected:
    // Full name and hdu of the image file
    std::string _filename;
    std::string _hdu;
    bool _is_gz;
    unsigned int _modify_time;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> _image;

    // Save image properties; only reopen for data or beams
    // Axes, dimension values
    casacore::IPosition _image_shape;
    size_t _num_dims, _image_plane_size;
    size_t _width, _height, _depth, _num_stokes;
    int _z_axis, _stokes_axis;
    std::vector<int> _render_axes;
    // Coordinate system
    casacore::CoordinateSystem _coord_sys;
    // Pixel mask
    bool _has_pixel_mask;

    // Storage for z-plane and cube statistics
    std::vector<std::vector<carta::FileInfo::ImageStats>> _z_stats;
    std::vector<carta::FileInfo::ImageStats> _cube_stats;

    // Storage for the stokes type vs. stokes index
    std::unordered_map<CARTA::PolarizationType, int> _stokes_indices;
    int _delta_stokes_index;

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

    // Basic flux density calculation
    double CalculateBeamArea();

    // Modify time changed
    bool ImageUpdated();
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
