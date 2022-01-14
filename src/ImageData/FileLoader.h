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

#include "ImageData/FileInfo.h"

namespace carta {

class Frame;

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
    virtual void SetStokesCrval(float stokes_crval);
    virtual void SetStokesCrpix(float stokes_crpix);
    virtual void SetStokesCdelt(int stokes_cdelt);
    virtual bool GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index);

    // Modify time changed
    bool ImageUpdated();

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
    std::vector<std::vector<FileInfo::ImageStats>> _z_stats;
    std::vector<FileInfo::ImageStats> _cube_stats;

    // Storage for the stokes type vs. stokes index
    std::unordered_map<CARTA::PolarizationType, int> _stokes_indices;
    float _stokes_crval;
    float _stokes_crpix;
    int _stokes_cdelt;

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
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
