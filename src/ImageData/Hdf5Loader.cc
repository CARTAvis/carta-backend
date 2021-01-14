/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Hdf5Loader.h"

namespace carta {

Hdf5Loader::Hdf5Loader(const std::string& filename) : FileLoader(filename), _hdu("0") {}

void Hdf5Loader::OpenFile(const std::string& hdu) {
    // Open hdf5 image with specified hdu
    if (!_image || (hdu != _hdu)) {
        _image.reset(new CartaHdf5Image(_filename, DataSetToString(FileInfo::Data::Image), hdu));
        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _hdu = hdu;

        // We need this immediately because dataSetToString uses it to find the name of the swizzled dataset
        _num_dims = _image->shape().size();

        // Load swizzled image lattice
        if (HasData(FileInfo::Data::SWIZZLED)) {
            _swizzled_image = std::unique_ptr<casacore::HDF5Lattice<float>>(
                new casacore::HDF5Lattice<float>(casacore::CountedPtr<casacore::HDF5File>(new casacore::HDF5File(_filename)),
                    DataSetToString(FileInfo::Data::SWIZZLED), hdu));
        }
    }
}

// We assume that the main image dataset is always loaded and therefore available.
// For everything else, we refer back to the file.
bool Hdf5Loader::HasData(FileInfo::Data ds) const {
    switch (ds) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            auto group_ptr = _image->Group();
            std::string data(DataSetToString(ds));
            if (data.empty()) {
                return false;
            }
            return casacore::HDF5Group::exists(*group_ptr, data);
    }
}

// TODO: when we fix the typing issue, this should probably return any dataset again, for consistency.
typename Hdf5Loader::ImageRef Hdf5Loader::GetImage() {
    // returns opened image as ImageInterface*
    return _image.get();
}

casacore::Lattice<float>* Hdf5Loader::LoadSwizzledData() {
    // swizzled data returns a Lattice
    return _swizzled_image.get();
}

std::string Hdf5Loader::DataSetToString(FileInfo::Data ds) const {
    static std::unordered_map<FileInfo::Data, std::string, EnumClassHash> um = {
        {FileInfo::Data::Image, "DATA"},
        {FileInfo::Data::YX, "SwizzledData/YX"},
        {FileInfo::Data::ZYX, "SwizzledData/ZYX"},
        {FileInfo::Data::ZYXW, "SwizzledData/ZYXW"},
        {FileInfo::Data::STATS, "Statistics"},
        {FileInfo::Data::STATS_2D, "Statistics/XY"},
        {FileInfo::Data::STATS_2D_MIN, "Statistics/XY/MIN"},
        {FileInfo::Data::STATS_2D_MAX, "Statistics/XY/MAX"},
        {FileInfo::Data::STATS_2D_SUM, "Statistics/XY/SUM"},
        {FileInfo::Data::STATS_2D_SUMSQ, "Statistics/XY/SUM_SQ"},
        {FileInfo::Data::STATS_2D_NANS, "Statistics/XY/NAN_COUNT"},
        {FileInfo::Data::STATS_2D_HIST, "Statistics/XY/HISTOGRAM"},
        {FileInfo::Data::STATS_2D_PERCENT, "Statistics/XY/PERCENTILES"},
        {FileInfo::Data::STATS_3D, "Statistics/XYZ"},
        {FileInfo::Data::STATS_3D_MIN, "Statistics/XYZ/MIN"},
        {FileInfo::Data::STATS_3D_MAX, "Statistics/XYZ/MAX"},
        {FileInfo::Data::STATS_3D_SUM, "Statistics/XYZ/SUM"},
        {FileInfo::Data::STATS_3D_SUMSQ, "Statistics/XYZ/SUM_SQ"},
        {FileInfo::Data::STATS_3D_NANS, "Statistics/XYZ/NAN_COUNT"},
        {FileInfo::Data::STATS_3D_HIST, "Statistics/XYZ/HISTOGRAM"},
        {FileInfo::Data::STATS_3D_PERCENT, "Statistics/XYZ/PERCENTILES"},
        {FileInfo::Data::RANKS, "PERCENTILE_RANKS"},
    };

    switch (ds) {
        case FileInfo::Data::XY:
            return _num_dims == 2 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZ:
            return _num_dims == 3 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZW:
            return _num_dims == 4 ? DataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::SWIZZLED:
            switch (_num_dims) {
                case 2:
                    return DataSetToString(FileInfo::Data::YX);
                case 3:
                    return DataSetToString(FileInfo::Data::ZYX);
                case 4:
                    return DataSetToString(FileInfo::Data::ZYXW);
                default:
                    return "";
            }
        default:
            return (um.find(ds) != um.end()) ? um[ds] : "";
    }
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
const Hdf5Loader::IPos Hdf5Loader::GetStatsDataShape(FileInfo::Data ds) {
    auto data_type = casacore::HDF5DataSet::getDataType(_image->Group()->getHid(), DataSetToString(ds));

    switch (data_type) {
        case casacore::TpInt: {
            return GetStatsDataShapeTyped<casacore::Int>(ds);
        }
        case casacore::TpInt64: {
            return GetStatsDataShapeTyped<casacore::Int64>(ds);
        }
        case casacore::TpFloat: {
            return GetStatsDataShapeTyped<casacore::Float>(ds);
        }
        case casacore::TpDouble: {
            return GetStatsDataShapeTyped<casacore::Double>(ds);
        }
        default: { throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype."); }
    }
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
casacore::ArrayBase* Hdf5Loader::GetStatsData(FileInfo::Data ds) {
    auto data_type = casacore::HDF5DataSet::getDataType(_image->Group()->getHid(), DataSetToString(ds));

    switch (data_type) {
        case casacore::TpInt: {
            return GetStatsDataTyped<casacore::Int, casacore::Int64>(ds);
        }
        case casacore::TpInt64: {
            return GetStatsDataTyped<casacore::Int64, casacore::Int64>(ds);
        }
        case casacore::TpFloat: {
            return GetStatsDataTyped<casacore::Float, casacore::Float>(ds);
        }
        case casacore::TpDouble: {
            return GetStatsDataTyped<casacore::Double, casacore::Float>(ds);
        }
        default: { throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype."); }
    }
}

bool Hdf5Loader::GetCursorSpectralData(
    std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex) {
    bool data_ok(false);
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (has_swizzled) {
        casacore::Slicer slicer;
        if (_num_dims == 4) {
            slicer = casacore::Slicer(IPos(4, 0, cursor_y, cursor_x, stokes), IPos(4, _num_channels, count_y, count_x, 1));
        } else if (_num_dims == 3) {
            slicer = casacore::Slicer(IPos(3, 0, cursor_y, cursor_x), IPos(3, _num_channels, count_y, count_x));
        }

        data.resize(_num_channels * count_y * count_x);
        casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        std::lock_guard<std::mutex> lguard(image_mutex);
        try {
            LoadSwizzledData()->doGetSlice(tmp, slicer);
            data_ok = true;
        } catch (casacore::AipsError& err) {
            std::cerr << "Could not load cursor spectral data from swizzled HDF5 dataset. AIPS ERROR: " << err.getMesg() << std::endl;
        }
    }
    return data_ok;
}

bool Hdf5Loader::UseRegionSpectralData(const IPos& region_shape, std::mutex& image_mutex) {
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return false;
    }

    int num_x = region_shape(0);
    int num_y = region_shape(1);
    int num_z = _num_channels;

    // Using the normal dataset may be faster if the region is wider than it is deep.
    // This is an initial estimate; we need to examine casacore's algorithm in more detail.
    if (num_y * num_z < num_x) {
        return false;
    }

    return true;
}

bool Hdf5Loader::GetRegionSpectralData(int region_id, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask, const IPos& origin,
    std::mutex& image_mutex, std::map<CARTA::StatsType, std::vector<double>>& results, float& progress) {
    // Return calculated stats if valid and complete,
    // or return accumulated stats for the next incomplete "x" slice of swizzled data (chan vs y).
    // Calling function should check for complete progress when x-range of region is complete
    // Mask is 2D mask for region only

    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return false;
    }

    // Check if region stats calculated
    auto region_stats_id = FileInfo::RegionStatsId(region_id, stokes);
    IPos mask_shape(mask.shape());
    if (_region_stats.count(region_stats_id) && _region_stats[region_stats_id].IsValid(origin, mask_shape) &&
        _region_stats[region_stats_id].IsCompleted()) {
        results = _region_stats[region_stats_id].stats;
        progress = PROFILE_COMPLETE;
        return true;
    }

    int num_x = mask_shape(0);
    int num_y = mask_shape(1);
    int num_z = _num_channels;
    double beam_area = CalculateBeamArea();
    bool has_flux = !std::isnan(beam_area);

    if (_region_stats.find(region_stats_id) == _region_stats.end()) { // region stats never calculated
        _region_stats.emplace(
            std::piecewise_construct, std::forward_as_tuple(region_id, stokes), std::forward_as_tuple(origin, mask_shape, num_z, has_flux));
    } else if (!_region_stats[region_stats_id].IsValid(origin, mask_shape)) { // region stats expired
        _region_stats[region_stats_id].origin = origin;
        _region_stats[region_stats_id].shape = mask_shape;
        _region_stats[region_stats_id].completed = false;
        _region_stats[region_stats_id].latest_x = 0;
    }

    int x_min = origin(0);
    int y_min = origin(1);

    auto& stats = _region_stats[region_stats_id].stats;
    auto& num_pixels = stats[CARTA::StatsType::NumPixels];
    auto& nan_count = stats[CARTA::StatsType::NanCount];
    auto& sum = stats[CARTA::StatsType::Sum];
    auto& mean = stats[CARTA::StatsType::Mean];
    auto& rms = stats[CARTA::StatsType::RMS];
    auto& sigma = stats[CARTA::StatsType::Sigma];
    auto& sum_sq = stats[CARTA::StatsType::SumSq];
    auto& min = stats[CARTA::StatsType::Min];
    auto& max = stats[CARTA::StatsType::Max];
    double* flux = has_flux ? stats[CARTA::StatsType::FluxDensity].data() : nullptr;

    // get the start of X
    size_t x_start = _region_stats[region_stats_id].latest_x;

    // Set initial values of stats, or those set to NAN in previous iterations
    for (size_t z = 0; z < num_z; z++) {
        if ((x_start == 0) || (num_pixels[z] == 0)) {
            min[z] = std::numeric_limits<float>::max();
            max[z] = std::numeric_limits<float>::lowest();
            num_pixels[z] = 0;
            nan_count[z] = 0;
            sum[z] = 0;
            sum_sq[z] = 0;
        }
    }

    // Lambda to calculate additional stats
    auto calculate_stats = [&]() {
        double sum_z, sum_sq_z;
        uint64_t num_pixels_z;

        for (size_t z = 0; z < num_z; z++) {
            if (num_pixels[z]) {
                sum_z = sum[z];
                sum_sq_z = sum_sq[z];
                num_pixels_z = num_pixels[z];

                mean[z] = sum_z / num_pixels_z;
                rms[z] = sqrt(sum_sq_z / num_pixels_z);
                sigma[z] = sqrt((sum_sq_z - (sum_z * sum_z / num_pixels_z)) / (num_pixels_z - 1));
                if (has_flux) {
                    flux[z] = sum_z / beam_area;
                }
            } else {
                // if there are no valid values, set all stats to NaN except the value and NaN counts
                for (auto& kv : stats) {
                    switch (kv.first) {
                        case CARTA::StatsType::NanCount:
                        case CARTA::StatsType::NumPixels:
                            break;
                        default:
                            kv.second[z] = NAN;
                            break;
                    }
                }
            }
        }
    };

    size_t delta_x = INIT_DELTA_CHANNEL; // since data is swizzled, third axis is x not channel
    size_t max_x = x_start + delta_x;
    if (max_x > num_x) {
        max_x = num_x;
    }
    std::vector<float> slice_data;

    for (size_t x = x_start; x < max_x; ++x) {
        if (!GetCursorSpectralData(slice_data, stokes, x + x_min, 1, y_min, num_y, image_mutex)) {
            return false;
        }

        for (size_t y = 0; y < num_y; y++) {
            // skip all Z values for masked pixels
            if (!mask.getAt(IPos(2, x, y))) {
                continue;
            }

            for (size_t z = 0; z < num_z; z++) {
                double v = slice_data[y * num_z + z];

                // skip all NaN pixels
                if (std::isfinite(v)) {
                    num_pixels[z] += 1;
                    sum[z] += v;
                    sum_sq[z] += v * v;

                    if (v < min[z]) {
                        min[z] = v;
                    } else if (v > max[z]) {
                        max[z] = v;
                    }
                }
            }
        }
    }

    // Calculate partial stats
    calculate_stats();

    results = _region_stats[region_stats_id].stats;
    if (max_x == num_x) {
        progress = PROFILE_COMPLETE;
    } else {
        progress = (float)max_x / num_x;
    }

    // Update starting x for next time
    _region_stats[region_stats_id].latest_x = max_x;

    if (progress >= PROFILE_COMPLETE) {
        // the stats calculation is completed
        _region_stats[region_stats_id].completed = true;
    }

    return true;
}

} // namespace carta
