/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FILELOADER_H_

#include <memory>
#include <string>

#include <casacore/casa/Utilities/DataType.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#include "ImageData/FileInfo.h"
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

class FileLoader {
public:
    using ImageRef = std::shared_ptr<casacore::ImageInterface<float>>;

    // directory only for ExprLoader, is_gz only for FitsLoader
    FileLoader(const std::string& filename, const std::string& directory = "", bool is_gz = false, bool is_generated = false);
    virtual ~FileLoader() = default;

    static FileLoader* GetLoader(const std::string& filename, const std::string& directory = "");
    // Access an image from the memory, not from the disk
    static FileLoader* GetLoader(std::shared_ptr<casacore::ImageInterface<float>> image, const std::string& filename);

    // check for mirlib (MIRIAD) error; returns true for other image types
    virtual bool CanOpenFile(std::string& error);
    // Open and close file
    virtual void OpenFile(const std::string& hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool HasData(FileInfo::Data ds) const;

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
    std::shared_ptr<casacore::CoordinateSystem> GetCoordinateSystem(const StokesSource& stokes_source = StokesSource());
    bool FindCoordinateAxes(casacore::IPosition& shape, std::vector<int>& spatial_axes, int& spectral_axis, int& stokes_axis,
        std::vector<int>& render_axes, int& z_axis, std::string& message);

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
    virtual bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex);
    // Check if one can apply swizzled data under such image format and region condition
    virtual bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex);
    virtual bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& image_mutex,
        std::map<CARTA::StatsType, std::vector<double>>& results, float& progress);
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

protected:
    // Full name and characteristics of the image file
    std::string _filename, _directory;
    std::string _hdu;
    bool _is_gz;
    bool _is_generated;
    unsigned int _modify_time;

    std::shared_ptr<casacore::ImageInterface<casacore::Float>> _image;

    // Computed stokes image
    std::shared_ptr<casacore::ImageInterface<float>> _computed_stokes_image;
    StokesSource _stokes_source;

    // Save image properties
    casacore::IPosition _image_shape;
    size_t _num_dims, _image_plane_size;
    size_t _width, _height, _depth, _num_stokes;
    int _z_axis, _stokes_axis;
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;
    bool _has_pixel_mask;
    casacore::DataType _data_type;

    // Storage for z-plane and cube statistics
    std::vector<std::vector<FileInfo::ImageStats>> _z_stats;
    std::vector<FileInfo::ImageStats> _cube_stats;
    FileInfo::ImageStats _empty_stats;

    // Storage for the stokes type vs. stokes index
    std::unordered_map<CARTA::PolarizationType, int> _stokes_indices;
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

    // Ignore the upper or lower cases of BUNIT from header
    void ResetBunit();
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
