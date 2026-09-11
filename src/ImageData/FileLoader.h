/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_FILELOADER_H_
#define CARTA_SRC_IMAGEDATA_FILELOADER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <casacore/casa/Utilities/DataType.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#include "ImageData/FileInfo.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "Util/Casacore.h"
#include "Util/Stokes.h"

namespace carta {

struct StokesSlicer {
    StokesSource stokes_source;
    casacore::Slicer slicer;

    StokesSlicer() {}
    StokesSlicer(StokesSource stokes_source_, casacore::Slicer slicer_) : stokes_source(stokes_source_), slicer(slicer_) {}
};

struct StokesRegion {
    StokesSource stokes_source;
    casacore::ImageRegion image_region;

    StokesRegion() {}
    StokesRegion(StokesSource stokes_source_, casacore::ImageRegion image_region_)
        : stokes_source(stokes_source_), image_region(image_region_) {}
};

// One 2D region of a batched spectral reduction: its bounding box in image pixels, plus an
// optional raster mask laid out row-major with x fastest, exactly as casacore's LCRegionFixed
// stores one. Both the mask and this struct are borrowed for the duration of the call.
//
// A null mask selects the whole bounding box, and that is a required case rather than a shortcut:
// an unrotated rectangle becomes an LCBox, whose getMask() is empty.
struct RegionMaskSpec {
    std::uint64_t x_start = 0;
    std::uint64_t y_start = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    const casacore::Bool* mask = nullptr;
    // The same selection in run-length form; see RegionMaskRuns. When these are set they are the
    // selection and `mask` is not read. Both or neither.
    const std::uint32_t* runs = nullptr;
    const std::uint64_t* run_offsets = nullptr;
    // Which way the runs lie. It has to match the image, so it is asked for rather than assumed.
    bool runs_along_y = false;
};

// A region mask as runs of selected pixels rather than a byte per pixel.
//
// Row r of the bounding box owns the runs at [offsets[r], offsets[r+1]), and run k is the half-open
// column range [runs[2k], runs[2k+1]). A reader that takes this form does not have to read a
// megabyte of raster to learn which chunks a region occupies, and every pixel of a run is selected,
// so it can accumulate one with the loop it uses for a region that has no mask at all.
//
// The obvious representation is worth it because regions are nearly convex: a rotated rectangle or
// an ellipse is one run per row whatever its size, so a thin band across a 7763x4742 image is 38 kB
// of runs against 36.8 MB of raster.
struct RegionMaskRuns {
    std::vector<std::uint32_t> runs;
    std::vector<std::uint64_t> offsets;

    bool Empty() const {
        return offsets.size() < 2;
    }
};

// Derive the runs of a casacore region mask. Returns an empty result for a mask that is not a
// contiguous two-dimensional array, which is the same case the callers already decline.
RegionMaskRuns RunsOfMask(const casacore::ArrayLattice<casacore::Bool>& mask, bool along_y);

// One run of channels of a batched reduction, for every region at once.
//
// num_pixels and sum are both laid out [region][channel] with region_stride doubles between
// regions, and point into the loader's own buffer: they are valid only until the callback returns.
// A channel whose region caught no valid pixel has num_pixels zero, which is the caller's signal
// that the mean it wants does not exist rather than a number to divide by.
struct RegionSpectralBlock {
    std::size_t first_channel = 0;
    std::size_t channel_count = 0;
    std::size_t region_count = 0;
    std::size_t region_stride = 0;
    const double* num_pixels = nullptr;
    const double* sum = nullptr;
};

class FileLoader {
public:
    using ImageRef = std::shared_ptr<casacore::ImageInterface<float>>;

    // directory only for ExprLoader, is_gz only for FitsLoader
    FileLoader(const std::string& filename, const std::string& directory = "", bool is_gz = false, bool is_generated = false);
    virtual ~FileLoader() = default;

    static std::shared_ptr<FileLoader> GetLoader(const std::string& filename, const std::string& directory = "");
    // Access an image from the memory, not from the disk
    static std::shared_ptr<FileLoader> GetLoader(std::shared_ptr<casacore::ImageInterface<float>> image, const std::string& filename);

    // check for mirlib (MIRIAD) error; returns true for other image types
    virtual bool CanOpenFile(std::string& error);
    // Open and close file
    virtual void OpenFile(const std::string& hdu);
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool HasData(FileInfo::Data ds) const;

    // Whether to set image beam from history headers
    void SetAipsBeamSupport(bool support);
    bool GetAipsBeamSupport();

    // If not in use, temp close image to prevent caching
    void CloseImageIfUpdated();

    // Return the opened casacore image or its class name
    ImageRef GetImage(bool check_data_type = true);
    casacore::DataType GetDataType();
    bool IsComplexDataType();

    // Return the opened casacore image or computed stokes image
    ImageRef GetStokesImage(const StokesSource& stokes_source);

    // read beam subtable
    bool GetBeams(std::vector<CARTA::Beam>& beams, std::string& error);

    // Image shape and coordinate system axes
    casacore::IPosition GetShape();
    AxesInfo GetAxes();
    DimsInfo GetDims();
    std::shared_ptr<casacore::CoordinateSystem> GetCoordinateSystem(const StokesSource& stokes_source = StokesSource());
    bool FindCoordinateAxes(std::string& message);

    // Slice image data (with mask applied)
    bool GetSlice(casacore::Array<float>& data, const StokesSlicer& stokes_slicer);

    // SubImage
    bool GetSubImage(const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image);
    bool GetSubImage(const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image);
    bool GetSubImage(const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image);

    // Image Statistics
    // Load image statistics, if they exist, from the file
    virtual void LoadImageStats(bool load_percentiles = false);
    // Retrieve stats for a particular channel or all channels
    virtual FileInfo::ImageStats& GetImageStats(int current_stokes, int channel);

    // Spectral profiles for cursor and region
    // Fill `data` with a cursor's or small box's spectrum.
    //
    // `partial_callback`, when given, is called as the profile fills, with the fraction of it that
    // is final: the leading portion of `data` up to that fraction is already correct and can be
    // forwarded. Returning false from it cancels the read. A loader that reads the whole profile in
    // one operation ignores it, which is honest -- it has no partial answer to offer.
    virtual bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex,
        const std::function<bool()>& cancellation_requested = {},
        const std::function<bool(float progress)>& partial_callback = {});
    // Check if one can apply swizzled data under such image format and region condition
    // Basic statistics for every plane of one stokes, in one pass over the pixels.
    //
    // The caller's own loop asks plane by plane, and each of those reads a whole plane into a
    // vector before reducing it. A loader that can walk the cube itself neither materialises a
    // plane nor reads one twice, which is what this exists for; `plane_callback` receives each
    // plane as it is finished and returning false from it cancels the walk.
    //
    // False means this loader has no such path and the caller should keep its own loop. A loader
    // that returns false must not have called the callback.
    virtual bool GetCubeBasicStats(
        int stokes, const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) {
        return false;
    }
    // Bin counts for every plane of one stokes over a fixed range, in one pass over the pixels.
    //
    // The companion to GetCubeBasicStats, and there for the same reason: the caller's own loop asks
    // plane by plane and each of those reads a whole plane into a vector first. `plane_callback`
    // receives each plane's bins as it is finished and returning false from it cancels.
    //
    // False means this loader has no such path. A loader that returns false must not have called
    // the callback.
    virtual bool GetCubeHistogram(int stokes, int num_bins, const HistogramBounds& bounds,
        const std::function<bool(int z, const std::vector<int>& bins)>& plane_callback) {
        return false;
    }
    // Whether a region handing this loader runs should lay them along y. See RegionMaskRuns.
    virtual bool SpectralRunsAlongY() const {
        return false;
    }
    virtual bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex);
    // `partial_callback`, when given, is called while one call is still working, with the profile
    // as it stands and how far along it is. A loader whose call is long enough that the caller's
    // own between-call checks would come too late reports through this instead; returning false
    // from it cancels the call. The values it carries are not final -- counts and sums grow and a
    // mean converges -- which is the same partial answer this interface already returns when it
    // reports progress below one.
    virtual bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& image_mutex,
        std::map<CARTA::StatsType, std::vector<double>>& results, float& progress,
        const std::function<bool(const std::map<CARTA::StatsType, std::vector<double>>&, float)>& partial_callback = {});
    // Reduce many regions over the same channels in one pass over the pixels, calling the sink with
    // a run of channels at a time. Returning false from the sink cancels the reduction.
    //
    // A position-velocity cut is one box per pixel along the line -- 5,792 of them across a 4096
    // pixel diagonal -- and they overlap heavily, so asking for them one at a time reads the same
    // chunks once per box. Default false: a loader whose format has no batched path keeps the
    // existing route, which is per-region and correct, just proportional to the region count.
    virtual bool GetMultiRegionSpectralData(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink);
    virtual bool GetDownsampledRasterData(
        std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex);
    virtual bool GetChunk(
        std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex);

    virtual bool HasMip(int mip) const;
    virtual bool UseTileCache() const;

    // Get the full name of image file
    std::string GetFileName();

    // Handle stokes type index
    virtual void SetStokesCrval(float stokes_crval);
    virtual void SetStokesCrpix(float stokes_crpix);
    virtual void SetStokesCdelt(int stokes_cdelt);
    virtual bool GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index);
    virtual bool GetStokesType(const int& stokes_index, CARTA::PolarizationType& stokes_type);
    std::unordered_map<CARTA::PolarizationType, int> GetStokesIndices() {
        return _stokes_indices;
    };

    // Modify time changed
    bool ImageUpdated();

    // Handle images created from LEL expression
    virtual bool SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message);

    bool IsGenerated() {
        return _is_generated;
    };

    bool IsHistoryBeam() {
        return _is_history_beam;
    }

    void SetHistoryBeam(const casacore::GaussianBeam& history_beam) {
        // For compressed fits.gz, where this is determined from headers only
        _is_history_beam = true;
        _history_beam = history_beam;
    }

protected:
    // Full name and characteristics of the image file
    std::string _filename, _directory;
    std::string _hdu;
    bool _is_gz;
    bool _is_generated;
    unsigned int _modify_time;

    // AIPS HISTORY beam support
    bool _support_aips_beam;
    bool _is_history_beam;
    casacore::GaussianBeam _history_beam; // for compressed fits file info

    std::shared_ptr<casacore::ImageInterface<casacore::Float>> _image;

    // Computed stokes image
    std::shared_ptr<casacore::ImageInterface<float>> _computed_stokes_image;
    StokesSource _stokes_source;

    // Save image properties
    casacore::IPosition _image_shape;
    size_t _num_dims;
    DimsInfo _dims;
    AxesInfo _axes;
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;
    bool _has_pixel_mask;
    casacore::DataType _data_type;

    // Storage for z-plane and cube statistics
    std::vector<std::vector<FileInfo::ImageStats>> _z_stats;
    std::vector<FileInfo::ImageStats> _cube_stats;
    FileInfo::ImageStats _empty_stats;

    // Storage for the stokes type vs. stokes index
    std::unordered_map<CARTA::PolarizationType, int> _stokes_indices;
    std::unordered_map<int, CARTA::PolarizationType> _stokes_types;
    float _stokes_crval;
    float _stokes_crpix;
    int _stokes_cdelt;

    // Return the shape of the specified stats dataset
    virtual const casacore::IPosition GetStatsDataShape(FileInfo::Data ds);

    // Return stats data as a casacore::Array of type casacore::Float or casacore::Int64
    virtual std::unique_ptr<casacore::ArrayBase> GetStatsData(FileInfo::Data ds);

    // Functions for loading individual types of statistics
    virtual void LoadStats2DBasic(FileInfo::Data ds);
    virtual void LoadStats2DHist();
    virtual void LoadStats2DPercent();
    virtual void LoadStats3DBasic(FileInfo::Data ds);
    virtual void LoadStats3DHist();
    virtual void LoadStats3DPercent();

    // Basic flux density calculation
    double CalculateBeamArea();

    // Set the image object and its parameters
    virtual void AllocateImage(const std::string& hdu) = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_FILELOADER_H_
