#include "FileLoader.h"

#include "CasaLoader.h"
#include "FitsLoader.h"
#include "Hdf5Loader.h"
#include "MiriadLoader.h"

using namespace carta;

FileLoader* FileLoader::GetLoader(const std::string& filename) {
    casacore::ImageOpener::ImageTypes type = FileInfo::fileType(filename);
    switch (type) {
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
            break;
        case casacore::ImageOpener::IMAGEEXPR:
            break;
        case casacore::ImageOpener::COMPLISTIMAGE:
            break;
        default:
            break;
    }
    return nullptr;
}

void FileLoader::FindCoords(int& spectral_axis, int& stokes_axis) {
    // use CoordinateSystem to determine axis coordinate types
    spectral_axis = -1;
    stokes_axis = -1;
    casacore::CoordinateSystem coord_sys;
    if (GetCoordinateSystem(coord_sys)) {
        // spectral axis
        spectral_axis = casacore::CoordinateUtil::findSpectralAxis(coord_sys);
        if (spectral_axis < 0) {
            int tab_coord = coord_sys.findCoordinate(casacore::Coordinate::TABULAR);
            if (tab_coord >= 0) {
                casacore::Vector<casacore::Int> pixel_axes = coord_sys.pixelAxes(tab_coord);
                for (casacore::uInt i = 0; i < pixel_axes.size(); ++i) {
                    casacore::String axis_name = coord_sys.worldAxisNames()(pixel_axes(i));
                    if (axis_name == "Frequency" || axis_name == "Velocity")
                        spectral_axis = pixel_axes(i);
                }
            }
        }
        // stokes axis
        int pixel, world, coord;
        casacore::CoordinateUtil::findStokesAxis(pixel, world, coord, coord_sys);
        if (coord >= 0)
            stokes_axis = pixel;

        // not found!
        if (spectral_axis < 2) {   // spectral not found or is xy
            if (stokes_axis < 2) { // stokes not found or is xy, use defaults
                spectral_axis = 2;
                stokes_axis = 3;
            } else { // stokes found, set spectral to other one
                if (stokes_axis == 2)
                    spectral_axis = 3;
                else
                    spectral_axis = 2;
            }
        } else if (stokes_axis < 2) { // stokes not found
            // set stokes to the other one
            if (spectral_axis == 2)
                stokes_axis = 3;
            else
                stokes_axis = 2;
        }
    }
}

bool FileLoader::FindShape(IPos& shape, int& spectral_axis, int& stokes_axis, std::string& message) {
    if (!HasData(FileInfo::Data::Image))
        return false;

    shape = LoadData(FileInfo::Data::Image)->shape();
    _num_dims = shape.size();

    if (_num_dims < 2 || _num_dims > 4) {
        message = "Image must be 2D, 3D, or 4D.";
        return false;
    }

    casacore::CoordinateSystem coord_sys;
    if (!GetCoordinateSystem(coord_sys)) {
        message = "Image does not have valid coordinate system.";
        return false;
    }
    if (coord_sys.nPixelAxes() != _num_dims) {
        message = "Problem loading image: incomplete header.";
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

    // spectral axis not defined if CTYPE is not "FREQ" (e.g. "FREQUENC" will fail)
    if ((spectral_axis == -1) && (_num_dims > 2)) {
        if ((_num_dims == 3) || (stokes_axis == 3)) {
            spectral_axis = 2;
        } else {
            spectral_axis = 3;
        }
    }

    if (spectral_axis == -1) {
        _num_channels = 1;
    } else {
        _num_channels = shape(spectral_axis);
    }

    if (stokes_axis == -1) {
        _num_stokes = 1;
    } else {
        _num_stokes = shape(stokes_axis);
    }

    _channel_size = shape(0) * shape(1);

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
                default: {}
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
                default: {}
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
                LoadStats2DBasic(FileInfo::Data::STATS_3D_SUM);
                LoadStats2DBasic(FileInfo::Data::STATS_3D_SUMSQ);
            }
            LoadStats3DBasic(FileInfo::Data::STATS_3D_NANS);

            LoadStats3DHist();

            if (load_percentiles) {
                LoadStats3DPercent();
            }

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

bool FileLoader::UseRegionSpectralData(const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, std::mutex& image_mutex) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::GetRegionSpectralData(int region_id, int config_stokes, int profile_stokes,
    const std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask, IPos origin, std::mutex& image_mutex,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>*, float)>& partial_results_callback) {
    // Must be implemented in subclasses
    return false;
}

void FileLoader::SetFramePtr(Frame* frame) {
    // Must be implemented in subclasses
}
