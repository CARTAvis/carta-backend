/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileLoader.h"

#include <cmath>

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>

#include "../Logger/Logger.h"
#include "../Util.h"
#include "CasaLoader.h"
#include "CompListLoader.h"
#include "ConcatLoader.h"
#include "ExprLoader.h"
#include "FitsLoader.h"
#include "Hdf5Loader.h"
#include "ImagePtrLoader.h"
#include "MiriadLoader.h"

using namespace carta;

FileLoader* FileLoader::GetLoader(const std::string& filename) {
    if (IsCompressedFits(filename)) {
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

FileLoader::FileLoader(const std::string& filename) : _filename(filename) {}

bool FileLoader::CanOpenFile(std::string& /*error*/) {
    return true;
}

bool FileLoader::GetShape(IPos& shape) {
    ImageRef image = GetImage();
    if (image) {
        shape = image->shape();
        return true;
    }
    return false;
}

bool FileLoader::GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) {
    ImageRef image = GetImage();
    if (image) {
        coord_sys = image->coordinates();
        return true;
    }
    return false;
}

bool FileLoader::FindCoordinateAxes(IPos& shape, int& spectral_axis, int& z_axis, int& stokes_axis, std::string& message) {
    // Return image shape and axes for image. Spectral axis may or may not be z axis.
    // All parameters are return values.
    spectral_axis = -1;
    z_axis = -1;
    stokes_axis = -1;

    if (!HasData(FileInfo::Data::Image)) {
        message = "Image has no data.";
        return false;
    }

    if (!GetShape(shape)) {
        message = "Cannot determine image shape.";
        return false;
    }

    // Dimension check
    _num_dims = shape.size();
    if (_num_dims < 2 || _num_dims > 4) {
        message = "Image must be 2D, 3D, or 4D.";
        return false;
    }

    // Coordinate system checks
    casacore::CoordinateSystem coord_sys;
    if (!GetCoordinateSystem(coord_sys)) {
        message = "Invalid coordinate system.";
        return false;
    }

    if (coord_sys.nPixelAxes() != _num_dims) {
        message = "Problem loading image: cannot determine coordinate axes from incomplete header.";
        return false;
    }

    // Determine which axes will be rendered
    std::vector<int> render_axes;
    GetRenderAxes(render_axes);
    size_t width = shape(render_axes[0]);
    size_t height = shape(render_axes[1]);
    _image_plane_size = width * height;

    // Spectral and stokes axis
    spectral_axis = coord_sys.spectralAxisNumber();
    stokes_axis = coord_sys.polarizationAxisNumber();

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
    if ((_stokes_indices.size() == 1) && (_delta_stokes_index > 0)) {
        auto it = _stokes_indices.begin();
        for (int i = 1; i < _num_stokes; ++i) {
            int stokes_value = GetStokesValue(it->first) + i * _delta_stokes_index;
            _stokes_indices[GetStokesType(stokes_value)] = i;
        }
    }
    return true;
}

void FileLoader::GetRenderAxes(std::vector<int>& axes) {
    // Determine which axes will be rendered
    if (!_render_axes.empty()) {
        axes = _render_axes;
        return;
    }

    // Default unless PV image
    axes.assign({0, 1});

    casacore::IPosition shape;
    if (GetShape(shape) && shape.size() > 2) {
        casacore::CoordinateSystem coord_system;
        if (GetCoordinateSystem(coord_system)) {
            // Check for PV image: [Linear, Spectral] axes
            if (coord_system.hasLinearCoordinate()) {
                // Returns -1 if no spectral axis
                int spectral_axis = coord_system.spectralAxisNumber();

                if (spectral_axis >= 0) {
                    // Find valid (not -1) linear axes
                    std::vector<int> valid_axes;
                    casacore::Vector<casacore::Int> lin_axes = coord_system.linearAxesNumbers();
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
    }

    _render_axes = axes;
}

bool FileLoader::GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer) {
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    try {
        if (data.shape() != slicer.length()) {
            data.resize(slicer.length());
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

bool FileLoader::GetSubImage(const casacore::Slicer& slicer, casacore::SubImage<float>& sub_image) {
    // Get SubImage from Slicer
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    sub_image = casacore::SubImage<float>(*image, slicer);
    return true;
}

bool FileLoader::GetSubImage(const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) {
    // Get SubImage from image region
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    sub_image = casacore::SubImage<float>(*image, region);
    return true;
}

bool FileLoader::GetSubImage(
    const casacore::Slicer& slicer, const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) {
    auto result = false;
    ImageRef image = GetImage();
    if (image) {
        auto temp_image = casacore::SubImage<float>(*image, region);
        sub_image = casacore::SubImage<float>(temp_image, slicer);
        result = true;
    }
    return result;
}

bool FileLoader::GetBeams(std::vector<CARTA::Beam>& beams, std::string& error) {
    // Obtains beam table from ImageInfo
    bool success(false);
    try {
        casacore::ImageInfo image_info = GetImage()->imageInfo();
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
            carta_beam.set_pa(gaussian_beam.getPA("deg").getValue());
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
                    carta_beam.set_pa(gaussian_beam.getPA("deg").getValue());
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

const FileLoader::IPos FileLoader::GetStatsDataShape(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsDataShape not implemented in this loader");
}

casacore::ArrayBase* FileLoader::GetStatsData(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsData not implemented in this loader");
}

void FileLoader::LoadStats2DBasic(FileInfo::Data ds) {
    if (HasData(ds)) {
        const IPos& stat_dims = GetStatsDataShape(ds);

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && stat_dims.size() == 0) || (_num_dims == 3 && stat_dims.isEqual(IPos(1, _depth))) ||
            (_num_dims == 4 && stat_dims.isEqual(IPos(2, _depth, _num_stokes)))) {
            auto data = GetStatsData(ds);

            switch (ds) {
                case FileInfo::Data::STATS_2D_MAX: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Max] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Min] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUM: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::Sum] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUMSQ: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t z = 0; z < _depth; z++) {
                            _z_stats[s][z].basic_stats[CARTA::StatsType::SumSq] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
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

            delete data;
        }
    }
}

void FileLoader::LoadStats2DHist() {
    FileInfo::Data ds = FileInfo::Data::STATS_2D_HIST;

    if (HasData(ds)) {
        const IPos& stat_dims = GetStatsDataShape(ds);
        size_t num_bins = stat_dims[0];

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && stat_dims.isEqual(IPos(1, num_bins))) || (_num_dims == 3 && stat_dims.isEqual(IPos(2, num_bins, _depth))) ||
            (_num_dims == 4 && stat_dims.isEqual(IPos(3, num_bins, _depth, _num_stokes)))) {
            auto data = static_cast<casacore::Array<casacore::Int64>*>(GetStatsData(ds));
            auto it = data->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t z = 0; z < _depth; z++) {
                    _z_stats[s][z].histogram_bins.resize(num_bins);
                    for (size_t b = 0; b < num_bins; b++) {
                        _z_stats[s][z].histogram_bins[b] = *it++;
                    }
                }
            }

            delete data;
        }
    }
}

// TODO: untested

void FileLoader::LoadStats2DPercent() {
    FileInfo::Data dsr = FileInfo::Data::RANKS;
    FileInfo::Data dsp = FileInfo::Data::STATS_2D_PERCENT;

    if (HasData(dsp) && HasData(dsr)) {
        const IPos& dims_vals = GetStatsDataShape(dsp);
        const IPos& dims_ranks = GetStatsDataShape(dsr);

        size_t num_ranks = dims_ranks[0];

        // We can handle 2D, 3D and 4D in the same way
        if ((_num_dims == 2 && dims_vals.isEqual(IPos(1, num_ranks))) ||
            (_num_dims == 3 && dims_vals.isEqual(IPos(2, num_ranks, _depth))) ||
            (_num_dims == 4 && dims_vals.isEqual(IPos(3, num_ranks, _depth, _num_stokes)))) {
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsr));
            auto data = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsp));

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

            delete ranks;
            delete data;
        }
    }
}

void FileLoader::LoadStats3DBasic(FileInfo::Data ds) {
    if (HasData(ds)) {
        const IPos& stat_dims = GetStatsDataShape(ds);

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && stat_dims.size() == 0) || (_num_dims == 4 && stat_dims.isEqual(IPos(1, _num_stokes)))) {
            auto data = GetStatsData(ds);

            switch (ds) {
                case FileInfo::Data::STATS_3D_MAX: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Max] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Min] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_SUM: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::Sum] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_SUMSQ: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::SumSq] = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].basic_stats[CARTA::StatsType::NanCount] = *it++;
                    }
                    break;
                }
                default:
                    break;
            }

            delete data;
        }
    }
}

void FileLoader::LoadStats3DHist() {
    FileInfo::Data ds = FileInfo::Data::STATS_3D_HIST;

    if (HasData(ds)) {
        const IPos& stat_dims = GetStatsDataShape(ds);
        size_t num_bins = stat_dims[0];

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && stat_dims.isEqual(IPos(1, num_bins))) ||
            (_num_dims == 4 && stat_dims.isEqual(IPos(2, num_bins, _num_stokes)))) {
            auto data = static_cast<casacore::Array<casacore::Int64>*>(GetStatsData(ds));
            auto it = data->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                _cube_stats[s].histogram_bins.resize(num_bins);
                for (size_t b = 0; b < num_bins; b++) {
                    _cube_stats[s].histogram_bins[b] = *it++;
                }
            }

            delete data;
        }
    }
}

// TODO: untested

void FileLoader::LoadStats3DPercent() {
    FileInfo::Data dsr = FileInfo::Data::RANKS;
    FileInfo::Data dsp = FileInfo::Data::STATS_2D_PERCENT;

    if (HasData(dsp) && HasData(dsr)) {
        const IPos& dims_vals = GetStatsDataShape(dsp);
        const IPos& dims_ranks = GetStatsDataShape(dsr);

        size_t nranks = dims_ranks[0];

        // We can handle 3D and 4D in the same way
        if ((_num_dims == 3 && dims_vals.isEqual(IPos(1, nranks))) || (_num_dims == 4 && dims_vals.isEqual(IPos(2, nranks, _num_stokes)))) {
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsr));
            auto data = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsp));

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

            delete ranks;
            delete data;
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
    return (z >= 0 ? _z_stats[current_stokes][z] : _cube_stats[current_stokes]);
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

std::string FileLoader::GetFileName() {
    return _filename;
}

double FileLoader::CalculateBeamArea() {
    ImageRef image = GetImage();
    auto& info = image->imageInfo();
    auto& coord = image->coordinates();

    if (!info.hasSingleBeam() || !coord.hasDirectionCoordinate()) {
        return NAN;
    }

    return info.getBeamAreaInPixels(-1, -1, coord.directionCoordinate());
}

void FileLoader::SetFirstStokesType(int stokes_value) {
    switch (stokes_value) {
        case 1:
            _stokes_indices[CARTA::StokesType::I] = 0;
            break;
        case 2:
            _stokes_indices[CARTA::StokesType::Q] = 0;
            break;
        case 3:
            _stokes_indices[CARTA::StokesType::U] = 0;
            break;
        case 4:
            _stokes_indices[CARTA::StokesType::V] = 0;
            break;
        default:
            break;
    }
}

bool FileLoader::GetStokesTypeIndex(const CARTA::StokesType& stokes_type, int& stokes_index) {
    if (_stokes_indices.count(stokes_type)) {
        stokes_index = _stokes_indices[stokes_type];
        return true;
    }
    return false;
}

void FileLoader::SetDeltaStokesIndex(int delta_stokes_index) {
    _delta_stokes_index = delta_stokes_index;
}
