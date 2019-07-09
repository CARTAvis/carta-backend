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

bool FileLoader::FindShape(IPos& shape, size_t& num_channels, size_t& num_stokes, int& spectral_axis, int& stokes_axis) {
    if (!HasData(FileInfo::Data::Image))
        return false;

    shape = LoadData(FileInfo::Data::Image)->shape();
    size_t num_dims = shape.size();

    if (num_dims < 2 || num_dims > 4) {
        return false;
    }

    // determine axis order (0-based)
    if (num_dims == 3) { // use defaults
        spectral_axis = 2;
        stokes_axis = -1;
    } else if (num_dims == 4) { // find spectral and stokes axes
        FindCoords(spectral_axis, stokes_axis);
    }

    num_channels = (spectral_axis >= 0 ? shape(spectral_axis) : 1);
    num_stokes = (stokes_axis >= 0 ? shape(stokes_axis) : 1);

    _num_dims = num_dims;
    _num_channels = num_channels;
    _num_stokes = num_stokes;

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
                            _channel_stats[s][c].max_val = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].min_val = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_MEAN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].mean = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::STATS_2D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        for (size_t c = 0; c < _num_channels; c++) {
                            _channel_stats[s][c].nan_count = *it++;
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
                        _cube_stats[s].max_val = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_MIN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].min_val = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_MEAN: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].mean = *it++;
                    }
                    break;
                }
                case FileInfo::Data::STATS_3D_NANS: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < _num_stokes; s++) {
                        _cube_stats[s].nan_count = *it++;
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

    if (HasData(FileInfo::Data::STATS)) {
        if (HasData(FileInfo::Data::STATS_2D)) {
            LoadStats2DBasic(FileInfo::Data::STATS_2D_MAX);
            LoadStats2DBasic(FileInfo::Data::STATS_2D_MIN);
            LoadStats2DBasic(FileInfo::Data::STATS_2D_MEAN);
            LoadStats2DBasic(FileInfo::Data::STATS_2D_NANS);

            LoadStats2DHist();

            if (load_percentiles) {
                LoadStats2DPercent();
            }

            // If we loaded all the 2D stats successfully, assume all channel stats are valid
            for (size_t s = 0; s < _num_stokes; s++) {
                for (size_t c = 0; c < _num_channels; c++) {
                    _channel_stats[s][c].valid = true;
                }
            }
        }

        if (HasData(FileInfo::Data::STATS_3D)) {
            LoadStats3DBasic(FileInfo::Data::STATS_3D_MAX);
            LoadStats3DBasic(FileInfo::Data::STATS_3D_MIN);
            LoadStats3DBasic(FileInfo::Data::STATS_3D_MEAN);
            LoadStats3DBasic(FileInfo::Data::STATS_3D_NANS);

            LoadStats3DHist();

            if (load_percentiles) {
                LoadStats3DPercent();
            }

            // If we loaded all the 3D stats successfully, assume all cube stats are valid
            for (size_t s = 0; s < _num_stokes; s++) {
                _cube_stats[s].valid = true;
            }
        }
    }
}

FileInfo::ImageStats& FileLoader::GetImageStats(int current_stokes, int channel) {
    return (channel >= 0 ? _channel_stats[current_stokes][channel] : _cube_stats[current_stokes]);
}

bool FileLoader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::CanUseSiwzzledData(const casacore::ArrayLattice<casacore::Bool>* mask) {
    // Must be implemented in subclasses
    return false;
}

bool FileLoader::GetRegionSpectralData(std::map<CARTA::StatsType, std::vector<double>>** stats_values,
    int stokes, int region_id, const casacore::ArrayLattice<casacore::Bool>* mask, IPos origin) {
    // Must be implemented in subclasses
    return false;
}

void FileLoader::SetRegionState(int region_id, std::string name, CARTA::RegionType type,
    std::vector<CARTA::Point> points, float rotation) {
    // Must be implemented in subclasses
}

void FileLoader::SetConnectionFlag(bool connected) {
    // Must be implemented in subclasses
}