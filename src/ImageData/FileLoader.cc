/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileLoader.h"

#include <cmath>

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>

#include "Logger/Logger.h"
#include "Util/File.h"

#include "CasaLoader.h"
#include "CompListLoader.h"
#include "ConcatLoader.h"
#include "ExprLoader.h"
#include "FitsLoader.h"
#include "Hdf5Loader.h"
#include "ImagePtrLoader.h"
#include "MiriadLoader.h"
#include "PolarizationCalculator.h"

using namespace carta;

FileLoader* FileLoader::GetLoader(const std::string& filename, const std::string& directory) {
    if (!directory.empty()) {
        // filename is LEL expression for image(s) in directory
        return new ExprLoader(filename, directory);
    } else if (IsCompressedFits(filename)) {
        return new FitsLoader(filename, true);
    }

    switch (CasacoreImageType(filename)) {
        case casacore::ImageOpener::AIPSPP:
            return new CasaLoader(filename);
        case casacore::ImageOpener::FITS:
            return new FitsLoader(filename);
        case casacore::ImageOpener::MIRIAD:
            return new MiriadLoader(filename);
        case casacore::ImageOpener::GIPSY:
            break;
        case casacore::ImageOpener::CAIPS:
            break;
        case casacore::ImageOpener::NEWSTAR:
            break;
        case casacore::ImageOpener::HDF5:
            return new Hdf5Loader(filename);
        case casacore::ImageOpener::IMAGECONCAT:
            return new ConcatLoader(filename);
        case casacore::ImageOpener::IMAGEEXPR:
            return new ExprLoader(filename);
        case casacore::ImageOpener::COMPLISTIMAGE:
            return new CompListLoader(filename);
        default:
            break;
    }
    return nullptr;
}

FileLoader* FileLoader::GetLoader(std::shared_ptr<casacore::ImageInterface<float>> image) {
    if (image) {
        return new ImagePtrLoader(image);
    } else {
        spdlog::error("Fail to assign an image pointer!");
        return nullptr;
    }
}

FileLoader::FileLoader(const std::string& filename, const std::string& directory, bool is_gz)
    : _filename(filename), _directory(directory), _is_gz(is_gz), _modify_time(0), _num_dims(0), _has_pixel_mask(false), _stokes_cdelt(0) {
    // Set initial modify time if filename is not LEL expression for file in directory
    if (directory.empty()) {
        ImageUpdated();
    }
}

bool FileLoader::CanOpenFile(std::string& /*error*/) {
    return true;
}

typename FileLoader::ImageRef FileLoader::GetImage(bool check_data_type) {
    if (!_image) {
        OpenFile(_hdu);
    }

    if (_image && check_data_type && (_data_type != _image->dataType()) && (_image->imageType() == "TempImage")) {
        // Check for CasaLoader workaround for non-float data; does not copy data into new image (for file list only)
        if (IsComplexDataType()) {
            throw(casacore::AipsError("Use image arithmetic to open images with complex data."));
        } else {
            throw(casacore::AipsError("Data type not supported."));
        }
    }

    return _image;
}

void FileLoader::CloseImageIfUpdated() {
    // Close image if updated when only the loader owns
    if (_image.unique() && ImageUpdated()) {
        _image->tempClose();
    }
}

bool FileLoader::ImageUpdated() {
    bool changed(false);

    // Do not close compressed image or run getstat on LEL ImageExpr (sets directory)
    if (_is_gz || !_directory.empty()) {
        return changed;
    }

    casacore::File ccfile(_filename);
    auto updated_time = ccfile.modifyTime();

    if (updated_time != _modify_time) {
        changed = true;
        _modify_time = updated_time;
    }

    return changed;
}

bool FileLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK: {
            return _has_pixel_mask;
        }
        default:
            break;
    }

    return false;
}

casacore::DataType FileLoader::GetDataType() {
    return _data_type;
}

bool FileLoader::IsComplexDataType() {
    return (_data_type == casacore::DataType::TpComplex) || (_data_type == casacore::DataType::TpDComplex);
}

casacore::IPosition FileLoader::GetShape() {
    return _image_shape;
}

std::shared_ptr<casacore::CoordinateSystem> FileLoader::GetCoordinateSystem(const StokesSource& stokes_source) {
    if (stokes_source.IsOriginalImage()) {
        return _coord_sys;
    } else {
        auto image = GetStokesImage(stokes_source);
        if (image) {
            return std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(image->coordinates().clone()));
        }
    }
    return std::make_shared<casacore::CoordinateSystem>();
}

bool FileLoader::FindCoordinateAxes(casacore::IPosition& shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message) {
    // Return image shape and axes for image. Spectral axis may or may not be z axis.
    // All parameters are return values.
    spectral_axis = -1;
    z_axis = -1;
    stokes_axis = -1;

    if (!HasData(FileInfo::Data::Image)) {
        message = "Image has no data.";
        return false;
    }

    shape = _image_shape;

    // Dimension check
    _num_dims = shape.size();
    if (_num_dims < 2 || _num_dims > 4) {
        message = "Image must be 2D, 3D, or 4D.";
        return false;
    }

    if (_coord_sys->nPixelAxes() != _num_dims) {
        message = "Problem loading image: cannot determine coordinate axes from incomplete header.";
        return false;
    }

    // Determine which axes will be rendered
    std::vector<int> render_axes = GetRenderAxes();
    _width = shape(render_axes[0]);
    _height = shape(render_axes[1]);
    _image_plane_size = _width * _height;

    // Spectral and stokes axis
    spectral_axis = _coord_sys->spectralAxisNumber();
    stokes_axis = _coord_sys->polarizationAxisNumber();

    // 2D image
    if (_num_dims == 2) {
        // Save z values
        _z_axis = -1;
        _depth = 1;
        // Save stokes values
        _stokes_axis = -1;
        _num_stokes = 1;
        return true;
    }

    // Cope with incomplete/invalid headers for 3D, 4D images
    bool no_spectral(spectral_axis < 0), no_stokes(stokes_axis < 0);
    if ((no_spectral && no_stokes) && (_num_dims == 3)) {
        // assume third is spectral with no stokes
        spectral_axis = 2;
    }

    if ((no_spectral || no_stokes) && (_num_dims == 4)) {
        if (no_spectral && !no_stokes) { // stokes is known
            spectral_axis = (stokes_axis == 3 ? 2 : 3);
        } else if (!no_spectral && no_stokes) { // spectral is known
            stokes_axis = (spectral_axis == 3 ? 2 : 3);
        } else { // neither is known
            // guess by shape (max 4 stokes)
            if (shape(2) > 4) {
                spectral_axis = 2;
                stokes_axis = 3;
            } else if (shape(3) > 4) {
                spectral_axis = 3;
                stokes_axis = 2;
            }

            if ((spectral_axis < 0) && (stokes_axis < 0)) {
                // could not guess, assume [spectral, stokes]
                spectral_axis = 2;
                stokes_axis = 3;
            }
        }
    }

    // Z axis is non-render axis that is not stokes (if any)
    for (size_t i = 0; i < _num_dims; ++i) {
        if ((i != render_axes[0]) && (i != render_axes[1]) && (i != stokes_axis)) {
            z_axis = i;
            break;
        }
    }

    // Save z axis values
    _z_axis = z_axis;
    _depth = (z_axis >= 0 ? shape(z_axis) : 1);

    // Save stokes axis values
    _stokes_axis = stokes_axis;
    _num_stokes = (stokes_axis >= 0 ? shape(stokes_axis) : 1);

    // save stokes types with respect to the stokes index
    if (_stokes_cdelt != 0) {
        for (int i = 0; i < _num_stokes; ++i) {
            int stokes_fits_value = _stokes_crval + (i + 1 - _stokes_crpix) * _stokes_cdelt;
            int stokes_value;
            if (FileInfo::ConvertFitsStokesValue(stokes_fits_value, stokes_value)) {
                _stokes_indices[GetStokesType(stokes_value)] = i;
            }
        }
    }

    return true;
}

std::vector<int> FileLoader::GetRenderAxes() {
    // Determine which axes will be rendered
    std::vector<int> axes;

    if (!_render_axes.empty()) {
        axes = _render_axes;
        return axes;
    }

    // Default unless PV image
    axes.assign({0, 1});

    if (_image_shape.size() > 2) {
        // Normally, use direction axes
        if (_coord_sys->hasDirectionCoordinate()) {
            casacore::Vector<casacore::Int> dir_axes = _coord_sys->directionAxesNumbers();
            axes[0] = dir_axes[0];
            axes[1] = dir_axes[1];
        } else if (_coord_sys->hasLinearCoordinate()) {
            // Check for PV image: [Linear, Spectral] axes
            // Returns -1 if no spectral axis
            int spectral_axis = _coord_sys->spectralAxisNumber();

            if (spectral_axis >= 0) {
                // Find valid (not -1) linear axes
                std::vector<int> valid_axes;
                casacore::Vector<casacore::Int> lin_axes = _coord_sys->linearAxesNumbers();
                for (auto axis : lin_axes) {
                    if (axis >= 0) {
                        valid_axes.push_back(axis);
                    }
                }

                // One linear + spectral axis = pV image
                if (valid_axes.size() == 1) {
                    valid_axes.push_back(spectral_axis);
                    axes = valid_axes;
                }
            }
        }
    }

    _render_axes = axes;
    return axes;
}

bool FileLoader::GetSlice(casacore::Array<float>& data, const StokesSlicer& stokes_slicer) {
    StokesSource stokes_source = stokes_slicer.stokes_source;
    casacore::Slicer slicer = stokes_slicer.slicer;
    try {
        auto image = GetStokesImage(stokes_source); // Get the opened image or computed stokes image from the original one

        if (!image) {
            return false;
        }

        if (data.shape() != slicer.length()) {
            data.resize(slicer.length());
        }

        auto image_type = image->imageType();
        if (image_type == "CartaFitsImage") {
            // Use cfitsio for slice
            return image->doGetSlice(data, slicer);
        } else if (image_type == "ImageExpr") {
            // Use ImageExpr for slice
            casacore::Array<float> slice_data;
            image->doGetSlice(slice_data, slicer);
            data = slice_data; // copy from reference
            return true;
        }

        // Get data slice with mask applied.
        // Apply slicer to image first to get appropriate cursor, and use read-only iterator
        casacore::SubImage<float> subimage(*image, slicer);
        casacore::RO_MaskedLatticeIterator<float> lattice_iter(subimage);

        for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
            casacore::Array<float> cursor_data = lattice_iter.cursor();

            if (image->isMasked()) {
                casacore::Array<float> masked_data(cursor_data); // reference the same storage
                const casacore::Array<bool> cursor_mask = lattice_iter.getMask();

                // Apply cursor mask to cursor data: set masked values to NaN.
                // booleans are used to delete copy of data if necessary
                bool del_mask_ptr;
                const bool* pCursorMask = cursor_mask.getStorage(del_mask_ptr);

                bool del_data_ptr;
                float* pMaskedData = masked_data.getStorage(del_data_ptr);

                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!pCursorMask[i]) {
                        pMaskedData[i] = NAN;
                    }
                }

                // free storage for cursor arrays
                cursor_mask.freeStorage(pCursorMask, del_mask_ptr);
                masked_data.putStorage(pMaskedData, del_data_ptr);
            }

            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            data(cursor_slicer) = cursor_data;
        }

        return true;
    } catch (casacore::AipsError& err) {
        spdlog::error("Error loading image data: {}", err.getMesg());
        return false;
    }
}

bool FileLoader::GetSubImage(const StokesSlicer& stokes_slicer, casacore::SubImage<float>& sub_image) {
    StokesSource stokes_source = stokes_slicer.stokes_source;
    casacore::Slicer slicer = stokes_slicer.slicer;

    // Get the opened casacore image or computed stokes image
    auto image = GetStokesImage(stokes_source);
    if (!image) {
        return false;
    }

    // Get SubImage from Slicer
    sub_image = casacore::SubImage<float>(*(image.get()), slicer);
    return true;
}

bool FileLoader::GetSubImage(const StokesRegion& stokes_region, casacore::SubImage<float>& sub_image) {
    StokesSource stokes_source = stokes_region.stokes_source;
    casacore::LattRegionHolder region = stokes_region.image_region;

    // Get the opened casacore image or computed stokes image
    auto image = GetStokesImage(stokes_source);
    if (!image) {
        return false;
    }

    // Get SubImage from image region
    sub_image = casacore::SubImage<float>(*(image.get()), region);
    return true;
}

bool FileLoader::GetSubImage(
    const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) {
    auto image = GetImage();
    if (!image) {
        return false;
    }

    auto temp_image = casacore::SubImage<float>(*(image.get()), region);
    sub_image = casacore::SubImage<float>(temp_image, slicer);
    return true;
}

bool FileLoader::GetBeams(std::vector<CARTA::Beam>& beams, std::string& error) {
    // Obtains beam table from ImageInfo
    bool success(false);
    try {
        auto image = GetImage();
        if (!image) {
            return success;
        }

        casacore::ImageInfo image_info = image->imageInfo();
        if (!image_info.hasBeam()) {
            error = "Image has no beam information.";
            return success;
        }

        if (image_info.hasSingleBeam()) {
            casacore::GaussianBeam gaussian_beam = image_info.restoringBeam();
            CARTA::Beam carta_beam;
            carta_beam.set_channel(-1);
            carta_beam.set_stokes(-1);
            carta_beam.set_major_axis(gaussian_beam.getMajor("arcsec"));
            carta_beam.set_minor_axis(gaussian_beam.getMinor("arcsec"));
            carta_beam.set_pa(gaussian_beam.getPA(casacore::Unit("deg")));
            beams.push_back(carta_beam);
        } else {
            casacore::ImageBeamSet beam_set = image_info.getBeamSet();
            casacore::GaussianBeam gaussian_beam;
            for (unsigned int stokes = 0; stokes < beam_set.nstokes(); ++stokes) {
                for (unsigned int chan = 0; chan < beam_set.nchan(); ++chan) {
                    gaussian_beam = beam_set.getBeam(chan, stokes);
                    CARTA::Beam carta_beam;
                    carta_beam.set_channel(chan);
                    carta_beam.set_stokes(stokes);
                    carta_beam.set_major_axis(gaussian_beam.getMajor("arcsec"));
                    carta_beam.set_minor_axis(gaussian_beam.getMinor("arcsec"));
                    carta_beam.set_pa(gaussian_beam.getPA(casacore::Unit("deg")));
                    beams.push_back(carta_beam);
                }
            }
        }
        success = true;
    } catch (casacore::AipsError& err) {
        error = "Image beam error: " + err.getMesg();
    }
    return success;
}

const casacore::IPosition FileLoader::GetStatsDataShape(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsDataShape not implemented in this loader");
}

std::unique_ptr<casacore::ArrayBase> FileLoader::GetStatsData(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsData not implemented in this loader");
}

void FileLoader::LoadStats2DBasic(FileInfo::Data ds) {
    if (HasData(ds)) {
        const casacore::IPosition& stat_dims = GetStatsDataShape(ds);

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && stat_dims.size() == 0) || (_num_dims == 3 && stat_dims.isEqual(casacore::IPosition(1, _depth))) ||
            (_num_dims == 4 && stat_dims.isEqual(casacore::IPosition(2, _depth, _num_stokes)))) {
            auto data = GetStatsData(ds);

            switch (ds) {
                case FileInfo::Data::STATS_2D_MAX: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Max] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Min] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUM: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Sum] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUMSQ: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::SumSq] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::NanCount] = *it++;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void FileLoader::LoadStats2DHist() {
    FileInfo::Data ds = FileInfo::Data::STATS_2D_HIST;

    if (HasData(ds)) {
        const casacore::IPosition& stat_dims = GetStatsDataShape(ds);
        size_t num_bins = stat_dims[0];

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && stat_dims.isEqual(casacore::IPosition(1, num_bins))) ||
            (_num_dims == 3 && stat_dims.isEqual(casacore::IPosition(2, num_bins, _depth))) ||
            (_num_dims == 4 && stat_dims.isEqual(casacore::IPosition(3, num_bins, _depth, _num_stokes)))) {
            auto data = GetStatsData(ds);
            auto stats_data = static_cast<casacore::Array<casacore::Int64>*>(data.get());
            auto it = stats_data->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t z = 0; z < _depth; z++) {
                    _z_stats[s][z].histogram_bins.resize(num_bins);
                    for (size_t b = 0; b < num_bins; b++) {
                        _z_stats[s][z].histogram_bins[b] = *it++;
                    }
                }
            }
        }
    }
}

// TODO: untested

void FileLoader::LoadStats2DPercent() {
    FileInfo::Data dsr = FileInfo::Data::RANKS;
    FileInfo::Data dsp = FileInfo::Data::STATS_2D_PERCENT;

    if (HasData(dsp) && HasData(dsr)) {
        const casacore::IPosition& dims_vals = GetStatsDataShape(dsp);
        const casacore::IPosition& dims_ranks = GetStatsDataShape(dsr);

        size_t num_ranks = dims_ranks[0];

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && dims_vals.isEqual(casacore::IPosition(1, num_ranks))) ||
            (_num_dims == 3 && dims_vals.isEqual(casacore::IPosition(2, num_ranks, _depth))) ||
            (_num_dims == 4 && dims_vals.isEqual(casacore::IPosition(3, num_ranks, _depth, _num_stokes)))) {
            auto ranks_data = GetStatsData(dsr);
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(ranks_data.get());
            auto stats_data = GetStatsData(dsp);
            auto data = static_cast<casacore::Array<casacore::Float>*>(stats_data.get());

            auto it = data->begin();
            auto itr = ranks->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t z = 0; z < _depth; z++) {
                    _z_stats[s][z].percentiles.resize(num_ranks);
                    _z_stats[s][z].percentile_ranks.resize(num_ranks);
                    for (size_t r = 0; r < num_ranks; r++) {
                        _z_stats[s][z].percentiles[r] = *it++;
                        _z_stats[s][z].percentile_ranks[r] = *itr++;
                    }
                }
            }
        }
    }
}

void FileLoader::LoadStats3DBasic(FileInfo::Data ds) {
    if (HasData(ds)) {
        const casacore::IPosition& stat_dims = GetStatsDataShape(ds);

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && stat_dims.size() == 0) || (_num_dims == 4 && stat_dims.isEqual(casacore::IPosition(1, _num_stokes)))) {
            auto data = GetStatsData(ds);

            switch (ds) {
                case FileInfo::Data::STATS_3D_MAX: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Max] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Min] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_SUM: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Sum] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_SUMSQ: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::SumSq] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data.get())->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::NanCount] = *it++;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void FileLoader::LoadStats3DHist() {
    FileInfo::Data ds = FileInfo::Data::STATS_3D_HIST;

    if (HasData(ds)) {
        const casacore::IPosition& stat_dims = GetStatsDataShape(ds);
        size_t num_bins = stat_dims[0];

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && stat_dims.isEqual(casacore::IPosition(1, num_bins))) ||
            (_num_dims == 4 && stat_dims.isEqual(casacore::IPosition(2, num_bins, _num_stokes)))) {
            auto stats_data = GetStatsData(ds);
            auto data = static_cast<casacore::Array<casacore::Int64>*>(stats_data.get());
            auto it = data->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                _cube_stats[s].histogram_bins.resize(num_bins);
                for (size_t b = 0; b < num_bins; b++) {
                    _cube_stats[s].histogram_bins[b] = *it++;
                }
            }
        }
    }
}

// TODO: untested

void FileLoader::LoadStats3DPercent() {
    FileInfo::Data dsr = FileInfo::Data::RANKS;
    FileInfo::Data dsp = FileInfo::Data::STATS_2D_PERCENT;

    if (HasData(dsp) && HasData(dsr)) {
        const casacore::IPosition& dims_vals = GetStatsDataShape(dsp);
        const casacore::IPosition& dims_ranks = GetStatsDataShape(dsr);

        size_t nranks = dims_ranks[0];

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && dims_vals.isEqual(casacore::IPosition(1, nranks))) ||
            (_num_dims == 4 && dims_vals.isEqual(casacore::IPosition(2, nranks, _num_stokes)))) {
            auto ranks_data = GetStatsData(dsr);
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(ranks_data.get());
            auto stats_data = GetStatsData(dsp);
            auto data = static_cast<casacore::Array<casacore::Float>*>(stats_data.get());

            auto it = data->begin();
            auto itr = ranks->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                _cube_stats[s].percentiles.resize(nranks);
                _cube_stats[s].percentile_ranks.resize(nranks);
                for (size_t r = 0; r < nranks; r++) {
                    _cube_stats[s].percentiles[r] = *it++;
                    _cube_stats[s].percentile_ranks[r] = *itr++;
                }
            }
        }
    }
}

void FileLoader::LoadImageStats(bool load_percentiles) {
    _z_stats.resize(_num_stokes);
    for (size_t s = 0; s < _num_stokes; s++) {
        _z_stats[s].resize(_depth);
    }
    _cube_stats.resize(_num_stokes);

    // Remove this check when we drop support for the old schema.
    // We assume that checking for only one of these datasets is sufficient.
    bool full(HasData(FileInfo::Data::STATS_2D_SUM));
    double sum, sum_sq, min, max;
    uint64_t num_pixels;

    if (HasData(FileInfo::Data::STATS)) {
        if (HasData(FileInfo::Data::STATS_2D)) {
            LoadStats2DBasic(FileInfo::Data::STATS_2D_MAX);
            LoadStats2DBasic(FileInfo::Data::STATS_2D_MIN);
            if (full) {
                LoadStats2DBasic(FileInfo::Data::STATS_2D_SUM);
                LoadStats2DBasic(FileInfo::Data::STATS_2D_SUMSQ);
            }
            LoadStats2DBasic(FileInfo::Data::STATS_2D_NANS);

            LoadStats2DHist();

            if (load_percentiles) {
                LoadStats2DPercent();
            }

            double beam_area = CalculateBeamArea();
            bool has_flux = !std::isnan(beam_area);

            // If we loaded all the 2D stats successfully, assume all channel stats are valid
            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t z = 0; z < _depth; z++) {
                    auto& stats = _z_stats[s][z].basic_stats;
                    if (full) {
                        num_pixels = _image_plane_size - stats[CARTA::StatsType::NanCount];
                        sum = stats[CARTA::StatsType::Sum];
                        sum_sq = stats[CARTA::StatsType::SumSq];
                        min = stats[CARTA::StatsType::Min];
                        max = stats[CARTA::StatsType::Max];

                        stats[CARTA::StatsType::NumPixels] = num_pixels;
                        stats[CARTA::StatsType::Mean] = sum / num_pixels;
                        stats[CARTA::StatsType::Sigma] = sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1));
                        stats[CARTA::StatsType::RMS] = sqrt(sum_sq / num_pixels);
                        stats[CARTA::StatsType::Extrema] = (abs(min) > abs(max) ? min : max);
                        if (has_flux) {
                            stats[CARTA::StatsType::FluxDensity] = sum / beam_area;
                        }

                        _z_stats[s][z].full = true;
                    }
                    _z_stats[s][z].valid = true;
                }
            }
        }

        if (HasData(FileInfo::Data::STATS_3D)) {
            LoadStats3DBasic(FileInfo::Data::STATS_3D_MAX);
            LoadStats3DBasic(FileInfo::Data::STATS_3D_MIN);
            if (full) {
                LoadStats3DBasic(FileInfo::Data::STATS_3D_SUM);
                LoadStats3DBasic(FileInfo::Data::STATS_3D_SUMSQ);
            }
            LoadStats3DBasic(FileInfo::Data::STATS_3D_NANS);

            LoadStats3DHist();

            if (load_percentiles) {
                LoadStats3DPercent();
            }

            double beam_area = CalculateBeamArea();
            bool has_flux = !std::isnan(beam_area);

            // If we loaded all the 3D stats successfully, assume all cube stats are valid
            for (size_t s = 0; s < _num_stokes; s++) {
                auto& stats = _cube_stats[s].basic_stats;
                if (full) {
                    num_pixels = (_image_plane_size * _depth) - stats[CARTA::StatsType::NanCount];
                    sum = stats[CARTA::StatsType::Sum];
                    sum_sq = stats[CARTA::StatsType::SumSq];
                    min = stats[CARTA::StatsType::Min];
                    max = stats[CARTA::StatsType::Max];

                    stats[CARTA::StatsType::NumPixels] = num_pixels;
                    stats[CARTA::StatsType::Mean] = sum / num_pixels;
                    stats[CARTA::StatsType::Sigma] = sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1));
                    stats[CARTA::StatsType::RMS] = sqrt(sum_sq / num_pixels);
                    stats[CARTA::StatsType::Extrema] = (abs(min) > abs(max) ? min : max);
                    if (has_flux) {
                        stats[CARTA::StatsType::FluxDensity] = sum / beam_area;
                    }

                    _cube_stats[s].full = true;
                }
                _cube_stats[s].valid = true;
            }
        }
    }
}

FileInfo::ImageStats& FileLoader::GetImageStats(int current_stokes, int z) {
    if (!IsComputedStokes(current_stokes)) { // Note: loader cache does not support the computed stokes
        return (z >= 0 ? _z_stats[current_stokes][z] : _cube_stats[current_stokes]);
    }
    return _empty_stats;
}

bool FileLoader::GetCursorSpectralData(
    std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) {
    // Must be implemented in subclasses; should call before GetRegionSpectralData
    return false;
}

bool FileLoader::GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::GetDownsampledRasterData(
    std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::GetChunk(
    std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::HasMip(int mip) const {
    return false;
}

bool FileLoader::UseTileCache() const {
    return false;
}

std::string FileLoader::GetFileName() {
    return _filename;
}

double FileLoader::CalculateBeamArea() {
    auto image = GetImage();
    if (!image) {
        return NAN;
    }

    auto& info = image->imageInfo();

    CloseImageIfUpdated();

    if (!info.hasSingleBeam() || !_coord_sys->hasDirectionCoordinate()) {
        return NAN;
    }

    return info.getBeamAreaInPixels(-1, -1, _coord_sys->directionCoordinate());
}

bool FileLoader::GetStokesTypeIndex(const CARTA::PolarizationType& stokes_type, int& stokes_index) {
    if (_stokes_indices.count(stokes_type)) {
        stokes_index = _stokes_indices[stokes_type];
        return true;
    }
    return false;
}

typename FileLoader::ImageRef FileLoader::GetStokesImage(const StokesSource& stokes_source) {
    if (stokes_source.IsOriginalImage()) {
        return GetImage();
    }

    if (_stokes_source != stokes_source) {
        // compute new stokes image with respect to the channel range
        carta::PolarizationCalculator polarization_calculator(
            GetImage(), AxisRange(stokes_source.z_range), AxisRange(stokes_source.x_range), AxisRange(stokes_source.y_range));

        if (stokes_source.stokes == COMPUTE_STOKES_PTOTAL) {
            _computed_stokes_image = polarization_calculator.ComputeTotalPolarizedIntensity();
        } else if (stokes_source.stokes == COMPUTE_STOKES_PFTOTAL) {
            _computed_stokes_image = polarization_calculator.ComputeTotalFractionalPolarizedIntensity();
        } else if (stokes_source.stokes == COMPUTE_STOKES_PLINEAR) {
            _computed_stokes_image = polarization_calculator.ComputePolarizedIntensity();
        } else if (stokes_source.stokes == COMPUTE_STOKES_PFLINEAR) {
            _computed_stokes_image = polarization_calculator.ComputeFractionalPolarizedIntensity();
        } else if (stokes_source.stokes == COMPUTE_STOKES_PANGLE) {
            _computed_stokes_image = polarization_calculator.ComputePolarizedAngle();
        } else {
            spdlog::error("Unknown computed stokes index {}", stokes_source.stokes);
            _computed_stokes_image = nullptr;
        }
        _stokes_source = stokes_source;
    }
    return _computed_stokes_image;
}

void FileLoader::SetStokesCrval(float stokes_crval) {
    _stokes_crval = stokes_crval;
}

void FileLoader::SetStokesCrpix(float stokes_crpix) {
    _stokes_crpix = stokes_crpix;
}

void FileLoader::SetStokesCdelt(int stokes_cdelt) {
    _stokes_cdelt = stokes_cdelt;
}

bool FileLoader::SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) {
    // Override in ExprLoader
    message = "Cannot save image type from loader.";
    return false;
}
