/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "LoaderHelper.h"

#include "Logger/Logger.h"

using namespace carta;

LoaderHelper::LoaderHelper(std::shared_ptr<FileLoader> loader, std::shared_ptr<ImageState> status, std::mutex& image_mutex)
    : _loader(loader), _image_state(status), _valid(true), _image_mutex(image_mutex) {
    if (!_loader || !_image_state) {
        _valid = false;
        spdlog::error("Image loader helper is invalid!");
    }
}

StokesSlicer LoaderHelper::GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes) {
    // Set stokes source for the image loader
    StokesSource stokes_source(stokes, z_range, x_range, y_range);

    // Slicer to apply z range and stokes to image shape
    // Start with entire image
    casacore::IPosition start(OriginalImageShape().size());
    start = 0;
    casacore::IPosition end(OriginalImageShape());
    end -= 1; // last position, not length

    // Slice x axis
    if (XAxis() >= 0) {
        int start_x(x_range.from), end_x(x_range.to);

        // Normalize x constants
        if (start_x == ALL_X) {
            start_x = 0;
        }
        if (end_x == ALL_X) {
            end_x = Width() - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(XAxis()) = start_x;
            end(XAxis()) = end_x;
        } else { // Reset the slice cut for the computed stokes image
            start(XAxis()) = 0;
            end(XAxis()) = end_x - start_x;
        }
    }

    // Slice y axis
    if (YAxis() >= 0) {
        int start_y(y_range.from), end_y(y_range.to);

        // Normalize y constants
        if (start_y == ALL_Y) {
            start_y = 0;
        }
        if (end_y == ALL_Y) {
            end_y = Height() - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(YAxis()) = start_y;
            end(YAxis()) = end_y;
        } else { // Reset the slice cut for the computed stokes image
            start(YAxis()) = 0;
            end(YAxis()) = end_y - start_y;
        }
    }

    // Slice z axis
    if (ZAxis() >= 0) {
        int start_z(z_range.from), end_z(z_range.to);

        // Normalize z constants
        if (start_z == ALL_Z) {
            start_z = 0;
        } else if (start_z == CURRENT_Z) {
            start_z = CurrentZ();
        }
        if (end_z == ALL_Z) {
            end_z = Depth() - 1;
        } else if (end_z == CURRENT_Z) {
            end_z = CurrentZ();
        }

        if (stokes_source.IsOriginalImage()) {
            start(ZAxis()) = start_z;
            end(ZAxis()) = end_z;
        } else { // Reset the slice cut for the computed stokes image
            start(ZAxis()) = 0;
            end(ZAxis()) = end_z - start_z;
        }
    }

    // Slice stokes axis
    if (StokesAxis() >= 0) {
        // Normalize stokes constant
        _image_state->CheckCurrentStokes(stokes);

        if (stokes_source.IsOriginalImage()) {
            start(StokesAxis()) = stokes;
            end(StokesAxis()) = stokes;
        } else {
            // Reset the slice cut for the computed stokes image
            start(StokesAxis()) = 0;
            end(StokesAxis()) = 0;
        }
    }

    // slicer for image data
    casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
    return StokesSlicer(stokes_source, section);
}

bool LoaderHelper::GetSlicerData(const StokesSlicer& stokes_slicer, float* data) {
    // Get image data with a slicer applied
    casacore::Array<float> tmp(stokes_slicer.slicer.length(), data, casacore::StorageInitPolicy::SHARE);
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool data_ok = _loader->GetSlice(tmp, stokes_slicer);
    _loader->CloseImageIfUpdated();
    ulock.unlock();
    return data_ok;
}

bool LoaderHelper::GetStokesTypeIndex(const string& coordinate, int& stokes_index, bool mute_err_msg) {
    // Coordinate could be profile (x, y, z), stokes string (I, Q, U), or combination (Ix, Qy)
    bool is_stokes_string = StokesStringTypes.find(coordinate) != StokesStringTypes.end();
    bool is_combination = (coordinate.size() > 1 && (coordinate.back() == 'x' || coordinate.back() == 'y' || coordinate.back() == 'z'));

    if (is_combination || is_stokes_string) {
        bool stokes_ok(false);

        std::string stokes_string;
        if (is_stokes_string) {
            stokes_string = coordinate;
        } else {
            stokes_string = coordinate.substr(0, coordinate.size() - 1);
        }

        if (StokesStringTypes.count(stokes_string)) {
            CARTA::PolarizationType stokes_type = StokesStringTypes[stokes_string];
            if (_loader->GetStokesTypeIndex(stokes_type, stokes_index)) {
                stokes_ok = true;
            } else if (IsComputedStokes(stokes_string)) {
                stokes_index = StokesStringTypes.at(stokes_string);
                stokes_ok = true;
            } else {
                int assumed_stokes_index = (StokesValues[stokes_type] - 1) % 4;
                if (NumStokes() > assumed_stokes_index) {
                    stokes_index = assumed_stokes_index;
                    stokes_ok = true;
                    spdlog::warn("Can not get stokes index from the header. Assuming stokes {} index is {}.", stokes_string, stokes_index);
                }
            }
        }
        if (!stokes_ok && !mute_err_msg) {
            spdlog::error("Spectral or spatial requirement {} failed: invalid stokes axis for image.", coordinate);
            return false;
        }
    } else {
        stokes_index = CurrentStokes(); // current stokes
    }
    return true;
}

bool LoaderHelper::TileCacheAvailable() {
    return _loader->UseTileCache() && _loader->HasMip(2);
}

double LoaderHelper::GetBeamArea() {
    return _loader->CalculateBeamArea();
}

bool LoaderHelper::FillFullImageCache(std::map<int, std::unique_ptr<float[]>>& stokes_data) {
    if (!stokes_data.empty()) {
        stokes_data.clear();
    }

    for (int stokes = 0; stokes < NumStokes(); ++stokes) {
        StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(ALL_Z), stokes);
        auto data_size = stokes_slicer.slicer.length().product();
        stokes_data[stokes] = std::make_unique<float[]>(data_size);
        if (!GetSlicerData(stokes_slicer, stokes_data[stokes].get())) {
            spdlog::error("Loading cube image failed (stokes index: {}).", stokes);
            return false;
        }
    }
    return true;
}

bool LoaderHelper::FillChannelImageCache(std::unique_ptr<float[]>& channel_data, int z, int stokes) {
    StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(z), stokes);
    auto data_size = stokes_slicer.slicer.length().product();
    channel_data = std::make_unique<float[]>(data_size);
    if (!GetSlicerData(stokes_slicer, channel_data.get())) {
        spdlog::error("Loading channel image failed (z: {}, stokes: {})", z, stokes);
        return false;
    }
    return true;
}

bool LoaderHelper::FillCubeImageCache(std::unique_ptr<float[]>& stokes_data, int stokes) {
    StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(ALL_Z), stokes);
    auto data_size = stokes_slicer.slicer.length().product();
    stokes_data = std::make_unique<float[]>(data_size);
    if (!GetSlicerData(stokes_slicer, stokes_data.get())) {
        spdlog::error("Loading cube image failed (stokes index: {}).", stokes);
        return false;
    }
    return true;
}

bool LoaderHelper::IsValid() const {
    return _valid;
}

void LoaderHelper::SetImageChannels(int z, int stokes) {
    _image_state->SetCurrentZ(z);
    _image_state->SetCurrentStokes(stokes);
}

// Image status parameters

casacore::IPosition LoaderHelper::OriginalImageShape() const {
    return _image_state->image_shape;
}

size_t LoaderHelper::Width() const {
    return _image_state->width;
}

size_t LoaderHelper::Height() const {
    return _image_state->height;
}

size_t LoaderHelper::Depth() const {
    return _image_state->depth;
}

size_t LoaderHelper::NumStokes() const {
    return _image_state->num_stokes;
}

int LoaderHelper::XAxis() const {
    return _image_state->x_axis;
}

int LoaderHelper::YAxis() const {
    return _image_state->y_axis;
}

int LoaderHelper::ZAxis() const {
    return _image_state->z_axis;
}

int LoaderHelper::SpectralAxis() const {
    return _image_state->spectral_axis;
}

int LoaderHelper::StokesAxis() const {
    return _image_state->stokes_axis;
}

int LoaderHelper::CurrentZ() const {
    return _image_state->z;
}

int LoaderHelper::CurrentStokes() const {
    return _image_state->stokes;
}

bool LoaderHelper::IsCurrentChannel(int z, int stokes) const {
    return _image_state->IsCurrentChannel(z, stokes);
}

bool LoaderHelper::IsCurrentStokes(int stokes) const {
    return _image_state->IsCurrentStokes(stokes);
}
