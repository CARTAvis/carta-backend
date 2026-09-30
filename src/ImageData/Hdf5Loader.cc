/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Hdf5Loader.h"

#include "../Logger/Logger.h"
#include "ImageStats/DerivedStatistics.h"
#include "Util/Image.h"
#include "Util/Nan.h"

namespace carta {

Hdf5Loader::Hdf5Loader(const std::string& filename) : FileLoader(filename), _hdu("0") {}

void Hdf5Loader::AllocateImage(const std::string& hdu) {
    // Explicitly handle empty HDU as the default 0
    std::string selected_hdu = hdu.empty() ? "0" : hdu;

    // Open hdf5 image with specified hdu
    if (!_image || (selected_hdu != _hdu)) {
        auto hdf5_image = new CartaHdf5Image(_filename, DataSetToString(FileInfo::Data::Image), selected_hdu);
        _image.reset(hdf5_image);
        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _hdu = selected_hdu;
        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));
        _data_type = hdf5_image->internalDataType();

        // Load swizzled image lattice
        if (HasData(FileInfo::Data::SWIZZLED)) {
            _swizzled_image = std::unique_ptr<casacore::HDF5Lattice<float>>(
                new casacore::HDF5Lattice<float>(std::shared_ptr<casacore::HDF5File>(new casacore::HDF5File(_filename)),
                    DataSetToString(FileInfo::Data::SWIZZLED), selected_hdu));
        }

        // save the data layout and known mips
        auto dset = hdf5_image->Lattice().array();
        _layout = H5Pget_layout(H5Dget_create_plist(dset->getHid()));

        if (HasData("MipMaps/DATA")) {
            casacore::HDF5Group mipmap_group(hdf5_image->Group()->getHid(), "MipMaps/DATA", true);
            for (auto& name : casacore::HDF5Group::linkNames(mipmap_group)) {
                std::regex re("DATA_XY_(\\d+)");
                std::smatch match;
                if (std::regex_match(name, match, re) && match.size() > 1) {
                    _mipmaps[std::stoi(match.str(1))] = std::unique_ptr<casacore::HDF5Lattice<float>>(
                        new casacore::HDF5Lattice<float>(std::shared_ptr<casacore::HDF5File>(new casacore::HDF5File(_filename)),
                            fmt::format("MipMaps/DATA/{}", name), selected_hdu));
                }
            }
        }
    }
}

bool Hdf5Loader::HasData(std::string ds_name) const {
    if (!_image) {
        return false;
    }

    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(_image.get());
    auto group_ptr = hdf5_image->Group();
    return casacore::HDF5Group::exists(*group_ptr, ds_name);
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
            return _has_pixel_mask;
        default:
            std::string ds_name(DataSetToString(ds));
            if (ds_name.empty()) {
                return false;
            }
            return HasData(ds_name);
    }
}

casacore::Lattice<float>* Hdf5Loader::LoadSwizzledData() {
    // swizzled data returns a Lattice
    return _swizzled_image.get();
}

casacore::Lattice<float>* Hdf5Loader::LoadMipMapData(int mip) {
    // mipmap data returns a Lattice
    return _mipmaps[mip].get();
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

bool Hdf5Loader::HasMip(int mip) const {
    return _mipmaps.find(mip) != _mipmaps.end();
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
const casacore::IPosition Hdf5Loader::GetStatsDataShape(FileInfo::Data ds) {
    auto image = GetImage();
    if (!image) {
        return casacore::IPosition();
    }

    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(image.get());
    auto data_type = casacore::HDF5DataSet::getDataType(hdf5_image->Group()->getHid(), DataSetToString(ds));

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
        default:
            throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype.");
    }
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the
// same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this
// complicated templating, at least for now.
std::unique_ptr<casacore::ArrayBase> Hdf5Loader::GetStatsData(FileInfo::Data ds) {
    auto image = GetImage();
    if (!image) {
        throw casacore::HDF5Error("Cannot get dataset " + DataSetToString(ds) + " from invalid image.");
    }

    CartaHdf5Image* hdf5_image = dynamic_cast<CartaHdf5Image*>(image.get());
    auto data_type = casacore::HDF5DataSet::getDataType(hdf5_image->Group()->getHid(), DataSetToString(ds));

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
        default:
            throw casacore::HDF5Error("Dataset " + DataSetToString(ds) + " has an unsupported datatype.");
    }
}

BatchOutcome Hdf5Loader::GetCursorSpectralData(std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y,
    std::mutex& image_mutex, const std::function<bool()>& /*cancellation_requested*/,
    const std::function<bool(float progress)>& /*partial_callback*/) {
    // The swizzled dataset hands over a whole spectrum in one operation, so there is no partial
    // answer to report and partial_callback is not called.
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return BatchOutcome::declined;
    }

    casacore::Slicer slicer;
    if (_num_dims == 4) {
        slicer = casacore::Slicer(
            casacore::IPosition(4, 0, cursor_y, cursor_x, stokes), casacore::IPosition(4, _dims.depth, count_y, count_x, 1));
    } else if (_num_dims == 3) {
        slicer = casacore::Slicer(casacore::IPosition(3, 0, cursor_y, cursor_x), casacore::IPosition(3, _dims.depth, count_y, count_x));
    }

    data.resize(_dims.depth * count_y * count_x);
    casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
    std::lock_guard<std::mutex> lguard(image_mutex);
    try {
        LoadSwizzledData()->doGetSlice(tmp, slicer);
    } catch (casacore::AipsError& err) {
        spdlog::warn("Could not load cursor spectral data from swizzled HDF5 dataset. AIPS ERROR: {}", err.getMesg());
        return BatchOutcome::failed;
    }
    return BatchOutcome::finished;
}

bool Hdf5Loader::UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) {
    std::unique_lock<std::mutex> ulock(image_mutex);
    bool has_swizzled = HasData(FileInfo::Data::SWIZZLED);
    ulock.unlock();
    if (!has_swizzled) {
        return false;
    }

    int width = region_shape(0);
    int height = region_shape(1);
    int depth = _dims.depth;

    // Using the normal dataset may be faster if the region is wider than it is deep.
    // This is an initial estimate; we need to examine casacore's algorithm in more detail.
    if (height * depth < width) {
        return false;
    }

    return true;
}

// A region's profile from the swizzled copy, a column of the region at a time: each column read is
// every channel of that column, so every channel asked for fills in together as the columns go by.
BatchOutcome Hdf5Loader::ReadOn(const RegionProfileRequest& request, std::mutex& image_mutex, RegionProfileProgress& progress,
    std::chrono::steady_clock::time_point deadline, const std::function<bool(double fraction)>& /*report*/) {
    // A column is read in one operation, so there is nothing to report while one is being read.
    if (!request.mask || request.origin.size() < 2) {
        return BatchOutcome::declined;
    }
    const auto& mask = *request.mask;
    const casacore::IPosition mask_shape(mask.shape());
    if (mask_shape.size() != 2 || !UseRegionSpectralData(mask_shape, image_mutex)) {
        return BatchOutcome::declined;
    }
    const auto& channels = request.channels;
    if (channels.from < 0 || channels.to < channels.from || channels.to >= static_cast<int>(_dims.depth) ||
        progress.channels.size() != static_cast<std::size_t>(channels.to - channels.from + 1)) {
        return BatchOutcome::declined;
    }

    const int width = mask_shape(0);
    const int height = mask_shape(1);
    const int x_min = request.origin(0);
    const int y_min = request.origin(1);
    if (progress.total == 0) {
        progress.total = static_cast<std::uint64_t>(width);
        for (auto& channel : progress.channels) {
            channel.read = true;
        }
    }

    std::vector<float> slice_data;
    do {
        const auto x = static_cast<int>(progress.done);
        if (GetCursorSpectralData(slice_data, request.stokes, x + x_min, 1, y_min, height, image_mutex) != BatchOutcome::finished) {
            return BatchOutcome::failed;
        }
        for (int y = 0; y < height; ++y) {
            // skip all Z values for masked pixels
            if (!mask.getAt(casacore::IPosition(2, x, y))) {
                continue;
            }
            // A column read holds every channel of the image, whichever were asked for.
            for (int z = channels.from; z <= channels.to; ++z) {
                const double v = slice_data[(static_cast<std::size_t>(y) * _dims.depth) + z];
                auto& channel = progress.channels[z - channels.from];
                if (std::isfinite(v)) {
                    channel.num_pixels += 1;
                    channel.sum += v;
                    channel.sum_sq += v * v;
                    channel.min = std::min(channel.min, v);
                    channel.max = std::max(channel.max, v);
                } else {
                    channel.nan_count += 1;
                }
            }
        }
        ++progress.done;
    } while (!progress.Complete() && std::chrono::steady_clock::now() < deadline);
    return BatchOutcome::finished;
}

double Hdf5Loader::BeamArea() {
    return CalculateBeamArea();
}

bool Hdf5Loader::GetDownsampledRasterData(
    std::vector<float>& data, int z, int stokes, CARTA::ImageBounds& bounds, int mip, std::mutex& image_mutex) {
    if (!HasMip(mip)) {
        return false;
    }

    bool data_ok(false);

    const int xmin = std::ceil((float)bounds.x_min() / mip);
    const int ymin = std::ceil((float)bounds.y_min() / mip);
    const int xmax = std::ceil((float)bounds.x_max() / mip);
    const int ymax = std::ceil((float)bounds.y_max() / mip);

    const int w = xmax - xmin;
    const int h = ymax - ymin;

    casacore::Slicer slicer;
    if (_num_dims == 4) {
        slicer = casacore::Slicer(casacore::IPosition(4, xmin, ymin, z, stokes), casacore::IPosition(4, w, h, 1, 1));
    } else if (_num_dims == 3) {
        slicer = casacore::Slicer(casacore::IPosition(3, xmin, ymin, z), casacore::IPosition(3, w, h, 1));
    } else if (_num_dims == 2) {
        slicer = casacore::Slicer(casacore::IPosition(2, xmin, ymin), casacore::IPosition(2, w, h));
    } else {
        return false;
    }

    data.resize(w * h);
    casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);

    std::lock_guard<std::mutex> lguard(image_mutex);
    try {
        LoadMipMapData(mip)->doGetSlice(tmp, slicer);
        data_ok = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Could not load MipMap data. AIPS ERROR: " << err.getMesg() << std::endl;
    }

    return data_ok;
}

bool Hdf5Loader::GetChunk(
    std::vector<float>& data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex) {
    bool data_ok(false);

    data_width = std::min(CHUNK_SIZE, (int)_dims.width - min_x);
    data_height = std::min(CHUNK_SIZE, (int)_dims.height - min_y);

    StokesSource stokes_source(stokes, AxisRange(z), AxisRange(min_x, min_x + data_width - 1), AxisRange(min_y, min_y + data_height - 1));
    if (!stokes_source.IsOriginalImage()) { // Reset the start position of the slicer as 0 for the computed stokes image
        stokes = 0;
        z = 0;
        min_x = 0;
        min_y = 0;
    }

    casacore::Slicer slicer;
    if (_num_dims == 4) {
        slicer = casacore::Slicer(casacore::IPosition(4, min_x, min_y, z, stokes), casacore::IPosition(4, data_width, data_height, 1, 1));
    } else if (_num_dims == 3) {
        slicer = casacore::Slicer(casacore::IPosition(3, min_x, min_y, z), casacore::IPosition(3, data_width, data_height, 1));
    } else if (_num_dims == 2) {
        slicer = casacore::Slicer(casacore::IPosition(2, min_x, min_y), casacore::IPosition(2, data_width, data_height));
    }

    data.resize(data_width * data_height);
    casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);

    std::lock_guard<std::mutex> lguard(image_mutex);
    try {
        GetSlice(tmp, StokesSlicer(stokes_source, slicer));
        data_ok = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Could not load image tile. AIPS ERROR: " << err.getMesg() << std::endl;
    }

    return data_ok;
}

bool Hdf5Loader::UseTileCache() const {
    return _layout == H5D_CHUNKED;
}

} // namespace carta
