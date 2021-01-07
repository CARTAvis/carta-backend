/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileLoader.h"

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>

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
        std::cerr << "Fail to assign an image pointer!" << std::endl;
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

bool FileLoader::FindCoordinateAxes(IPos& shape, int& spectral_axis, int& stokes_axis, std::string& message) {
    // Return image shape, spectral axis, and stokes axis from image data, coordinate system, and extended file info.

    // set defaults: undefined
    spectral_axis = -1;
    stokes_axis = -1;

    if (!HasData(FileInfo::Data::Image)) {
        return false;
    }

    if (!GetShape(shape)) {
        return false;
    }

    _num_dims = shape.size();

    if (_num_dims < 2 || _num_dims > 4) {
        message = "Image must be 2D, 3D, or 4D.";
        return false;
    }
    _channel_size = shape(0) * shape(1);

    casacore::CoordinateSystem coord_sys;
    if (!GetCoordinateSystem(coord_sys)) {
        message = "Image does not have valid coordinate system.";
        return false;
    }
    if (coord_sys.nPixelAxes() != _num_dims) {
        message = "Problem loading image: cannot determine coordinate axes from incomplete header.";
        return false;
    }

    // use CoordinateSystem to find coordinate axes
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    spectral_axis = coord_sys.spectralAxisNumber();
    stokes_axis = coord_sys.polarizationAxisNumber();

    // pv images not supported (yet); spectral axis is 0 or 1, and the other is linear
    if (!linear_axes.empty() && (((spectral_axis == 0) && (linear_axes(0) == 1)) || ((spectral_axis == 1) && (linear_axes(0) == 0)))) {
        message = "Position-velocity (pv) images not supported yet.";
        return false;
    }

    // 2D image
    if (_num_dims == 2) {
        _num_channels = 1;
        _num_stokes = 1;
        _spectral_axis = spectral_axis;
        _stokes_axis = stokes_axis;
        return true;
    }

    // 3D image
    if (_num_dims == 3) {
        spectral_axis = (spectral_axis < 0 ? 2 : spectral_axis);
        _num_channels = shape(spectral_axis);
        _num_stokes = 1;
        _spectral_axis = spectral_axis;
        _stokes_axis = stokes_axis;
        return true;
    }

    // 4D image
    if ((spectral_axis < 0) || (stokes_axis < 0)) {
        if ((spectral_axis < 0) && (stokes_axis >= 0)) { // stokes is known
            spectral_axis = (stokes_axis == 3 ? 2 : 3);
        } else if ((spectral_axis >= 0) && (stokes_axis < 0)) { // spectral is known
            stokes_axis = (spectral_axis == 3 ? 2 : 3);
        }
        if ((spectral_axis < 0) && (stokes_axis < 0)) { // neither is known, guess by shape (max 4 stokes)
            if (shape(2) > 4) {
                spectral_axis = 2;
                stokes_axis = 3;
            } else if (shape(3) > 4) {
                spectral_axis = 3;
                stokes_axis = 2;
            }
        }
        if ((spectral_axis < 0) && (stokes_axis < 0)) { // neither is known, give up
            message = "Problem loading image: cannot determine coordinate axes from incomplete header.";
            return false;
        }
    }
    _num_channels = (spectral_axis == -1 ? 1 : shape(spectral_axis));
    _num_stokes = (stokes_axis == -1 ? 1 : shape(stokes_axis));
    _spectral_axis = spectral_axis;
    _stokes_axis = stokes_axis;

    return true;
}

bool FileLoader::GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer) {
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    if (data.shape() != slicer.length()) {
        try {
            data.resize(slicer.length());
        } catch (casacore::AipsError& err) {
            return false;
        }
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
                    pMaskedData[i] = std::numeric_limits<float>::quiet_NaN();
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
}

bool FileLoader::GetSubImage(const casacore::Slicer& slicer, casacore::SubImage<float>& sub_image) {
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    sub_image = casacore::SubImage<float>(*image, slicer);
    return true;
}

bool FileLoader::GetSubImage(const casacore::LattRegionHolder& region, casacore::SubImage<float>& sub_image) {
    ImageRef image = GetImage();
    if (!image) {
        return false;
    }

    sub_image = casacore::SubImage<float>(*image, region);
    return true;
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
        if ((_num_dims == 2 && stat_dims.size() == 0) || (_num_dims == 3 && stat_dims.isEqual(IPos(1, _num_channels))) ||
            (_num_dims == 4 && stat_dims.isEqual(IPos(2, _num_channels, _num_stokes)))) {
            auto data = GetStatsData(ds);

            switch (ds) {
                case FileInfo::Data::STATS_2D_MAX: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].basic_stats[CARTA::StatsType::Max] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].basic_stats[CARTA::StatsType::Min] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUM: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].basic_stats[CARTA::StatsType::Sum] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_SUMSQ: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].basic_stats[CARTA::StatsType::SumSq] = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].basic_stats[CARTA::StatsType::NanCount] = *it++;
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
        if ((_num_dims == 2 && stat_dims.isEqual(IPos(1, num_bins))) ||
            (_num_dims == 3 && stat_dims.isEqual(IPos(2, num_bins, _num_channels))) ||
            (_num_dims == 4 && stat_dims.isEqual(IPos(3, num_bins, _num_channels, _num_stokes)))) {
            auto data = static_cast<casacore::Array<casacore::Int64>*>(GetStatsData(ds));
            auto it = data->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t c = 0; c < _num_channels; c++) {
                    _channel_stats[s][c].histogram_bins.resize(num_bins);
                    for (size_t b = 0; b < num_bins; b++) {
                        _channel_stats[s][c].histogram_bins[b] = *it++;
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
            (_num_dims == 3 && dims_vals.isEqual(IPos(2, num_ranks, _num_channels))) ||
            (_num_dims == 4 && dims_vals.isEqual(IPos(3, num_ranks, _num_channels, _num_stokes)))) {
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsr));
            auto data = static_cast<casacore::Array<casacore::Float>*>(GetStatsData(dsp));

            auto it = data->begin();
            auto itr = ranks->begin();

            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t c = 0; c < _num_channels; c++) {
                    _channel_stats[s][c].percentiles.resize(num_ranks);
                    _channel_stats[s][c].percentile_ranks.resize(num_ranks);
                    for (size_t r = 0; r < num_ranks; r++) {
                        _channel_stats[s][c].percentiles[r] = *it++;
                        _channel_stats[s][c].percentile_ranks[r] = *itr++;
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
    _channel_stats.resize(_num_stokes);
    for (size_t s = 0; s < _num_stokes; s++) {
        _channel_stats[s].resize(_num_channels);
    }
    _cube_stats.resize(_num_stokes);

    // Remove this check when we drop support for the old schema.
    // We assume that checking for only one of these datasets is sufficient.
    bool full(HasData(FileInfo::Data::STATS_2D_SUM));
    double sum, sum_sq;
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
                for (size_t c = 0; c < _num_channels; c++) {
                    auto& stats = _channel_stats[s][c].basic_stats;
                    if (full) {
                        num_pixels = _channel_size - stats[CARTA::StatsType::NanCount];
                        sum = stats[CARTA::StatsType::Sum];
                        sum_sq = stats[CARTA::StatsType::SumSq];

                        stats[CARTA::StatsType::NumPixels] = num_pixels;
                        stats[CARTA::StatsType::Mean] = sum / num_pixels;
                        stats[CARTA::StatsType::Sigma] = sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1));
                        stats[CARTA::StatsType::RMS] = sqrt(sum_sq / num_pixels);
                        if (has_flux) {
                            stats[CARTA::StatsType::FluxDensity] = sum / beam_area;
                        }

                        _channel_stats[s][c].full = true;
                    }
                    _channel_stats[s][c].valid = true;
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
                    num_pixels = (_channel_size * _num_channels) - stats[CARTA::StatsType::NanCount];
                    sum = stats[CARTA::StatsType::Sum];
                    sum_sq = stats[CARTA::StatsType::SumSq];

                    stats[CARTA::StatsType::NumPixels] = num_pixels;
                    stats[CARTA::StatsType::Mean] = sum / num_pixels;
                    stats[CARTA::StatsType::Sigma] = sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1));
                    stats[CARTA::StatsType::RMS] = sqrt(sum_sq / num_pixels);
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

FileInfo::ImageStats& FileLoader::GetImageStats(int current_stokes, int channel) {
    return (channel >= 0 ? _channel_stats[current_stokes][channel] : _cube_stats[current_stokes]);
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
