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
#include "Util/Image.h"

namespace carta {

class Frame;

struct StokesSlicer {
    StokesSource stokes_source;
    casacore::Slicer slicer;

    StokesSlicer() {}
    StokesSlicer(StokesSource stokes_source_, casacore::Slicer slicer_) : stokes_source(stokes_source_), slicer(slicer_) {}
    bool operator==(const StokesSlicer& rhs) const {
        return (stokes_source == rhs.stokes_source) && (slicer == rhs.slicer);
    }
    bool operator!=(const StokesSlicer& rhs) const {
        return !(*this == rhs);
    }
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
    virtual ~FileLoader() = default;
    virtual bool CanOpenFile(std::string& error) = 0;
    virtual void OpenFile(const std::string& hdu) = 0;
    virtual bool HasData(FileInfo::Data ds) const = 0;
    virtual void CloseImageIfUpdated() = 0;
    virtual ImageRef GetImage(bool check_data_type = true) = 0;
    virtual casacore::DataType GetDataType() = 0;
    virtual bool IsComplexDataType() = 0;
    virtual ImageRef GetStokesImage(const StokesSource& stokes_source) = 0;
    virtual bool GetBeams(std::vector<CARTA::Beam>& beams, std::string& error) = 0;
    virtual casacore::IPosition GetShape() = 0;
    virtual std::shared_ptr<casacore::CoordinateSystem> GetCoordinateSystem(const StokesSource& stokes_source = StokesSource()) = 0;
    virtual bool FindCoordinateAxes(
        casacore::IPosition& shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message) = 0;
    virtual std::vector<int> GetRenderAxes() = 0;
    virtual bool GetSlice(casacore::Array<float>& data, const StokesSlicer& stokes_slicer) = 0;
    virtual bool GetSubImage(const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image) = 0;
    virtual bool GetSubImage(const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image) = 0;
    virtual bool GetSubImage(
        const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) = 0;
    virtual void LoadImageStats(bool load_percentiles = false) = 0;
    virtual FileInfo::ImageStats& GetImageStats(int current_stokes, int channel) = 0;
    virtual bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) = 0;
    virtual bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) = 0;
    virtual bool GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress) = 0;
    virtual bool GetDownsampledRasterData(
        std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex) = 0;
    virtual bool GetChunk(
        std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex) = 0;
    virtual bool HasMip(int mip) const = 0;
    virtual bool UseTileCache() const = 0;
    virtual std::string GetFileName() = 0;
    virtual void SetStokesCrval(float stokes_crval) = 0;
    virtual void SetStokesCrpix(float stokes_crpix) = 0;
    virtual void SetStokesCdelt(int stokes_cdelt) = 0;
    virtual bool GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index) = 0;
    virtual std::unordered_map<CARTA::PolarizationType, int> GetStokesIndices() = 0;
    virtual bool ImageUpdated() = 0;
    virtual bool SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) = 0;

protected:
    virtual const casacore::IPosition GetStatsDataShape(FileInfo::Data ds) = 0;
    virtual std::unique_ptr<casacore::ArrayBase> GetStatsData(FileInfo::Data ds) = 0;
    virtual void LoadStats2DBasic(FileInfo::Data ds) = 0;
    virtual void LoadStats2DHist() = 0;
    virtual void LoadStats2DPercent() = 0;
    virtual void LoadStats3DBasic(FileInfo::Data ds) = 0;
    virtual void LoadStats3DHist() = 0;
    virtual void LoadStats3DPercent() = 0;
    virtual double CalculateBeamArea() = 0;
};

class BaseFileLoader : public FileLoader {
public:
    // directory only for ExprLoader, is_gz only for FitsLoader
    BaseFileLoader(const std::string& filename, const std::string& directory = "", bool is_gz = false);

    static FileLoader* GetLoader(const std::string& filename, const std::string& directory = "");
    // Access an image from the memory, not from the disk
    static FileLoader* GetLoader(std::shared_ptr<casacore::ImageInterface<float>> image);

    // check for mirlib (MIRIAD) error; returns true for other image types
    bool CanOpenFile(std::string& error) override;
    // Check to see if the file has a particular HDU/group/table/etc
    bool HasData(FileInfo::Data ds) const override;

    // If not in use, temp close image to prevent caching
    void CloseImageIfUpdated() override;

    // Return the opened casacore image or its class name
    ImageRef GetImage(bool check_data_type = true) override;
    casacore::DataType GetDataType() override;
    bool IsComplexDataType() override;

    // Return the opened casacore image or computed stokes image
    ImageRef GetStokesImage(const StokesSource& stokes_source) override;

    // read beam subtable
    bool GetBeams(std::vector<CARTA::Beam>& beams, std::string& error) override;

    // Image shape and coordinate system axes
    casacore::IPosition GetShape() override;
    std::shared_ptr<casacore::CoordinateSystem> GetCoordinateSystem(const StokesSource& stokes_source = StokesSource()) override;
    bool FindCoordinateAxes(casacore::IPosition& shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message) override;
    std::vector<int> GetRenderAxes() override; // Determine axes used for image raster data

    // Slice image data (with mask applied)
    bool GetSlice(casacore::Array<float>& data, const StokesSlicer& stokes_slicer) override;

    // SubImage
    bool GetSubImage(const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image) override;
    bool GetSubImage(const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image) override;
    bool GetSubImage(
        const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) override;

    // Image Statistics
    // Load image statistics, if they exist, from the file
    void LoadImageStats(bool load_percentiles = false) override;
    // Retrieve stats for a particular channel or all channels
    FileInfo::ImageStats& GetImageStats(int current_stokes, int channel) override;

    // Spectral profiles for cursor and region
    bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) override;
    // Check if one can apply swizzled data under such image format and region condition
    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results,
        float& progress) override;
    bool GetDownsampledRasterData(
        std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex) override;
    bool GetChunk(std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes,
        std::mutex& image_mutex) override;

    bool HasMip(int mip) const override;
    bool UseTileCache() const override;

    // Get the full name of image file
    std::string GetFileName() override;

    // Handle stokes type index
    void SetStokesCrval(float stokes_crval) override;
    void SetStokesCrpix(float stokes_crpix) override;
    void SetStokesCdelt(int stokes_cdelt) override;
    bool GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index) override;
    std::unordered_map<CARTA::PolarizationType, int> GetStokesIndices() override {
        return _stokes_indices;
    };

    // Modify time changed
    bool ImageUpdated() override;

    // Handle images created from LEL expression
    bool SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) override;

protected:
    // Full name and characteristics of the image file
    std::string _filename, _directory;
    std::string _hdu;
    bool _is_gz;
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
    std::vector<int> _render_axes;
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
    const casacore::IPosition GetStatsDataShape(FileInfo::Data ds) override;

    // Return stats data as a casacore::Array of type casacore::Float or casacore::Int64
    std::unique_ptr<casacore::ArrayBase> GetStatsData(FileInfo::Data ds) override;

    // Functions for loading individual types of statistics
    void LoadStats2DBasic(FileInfo::Data ds) override;
    void LoadStats2DHist() override;
    void LoadStats2DPercent() override;
    void LoadStats3DBasic(FileInfo::Data ds) override;
    void LoadStats3DHist() override;
    void LoadStats3DPercent() override;

    // Basic flux density calculation
    double CalculateBeamArea() override;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
