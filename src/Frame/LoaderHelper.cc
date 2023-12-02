/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "LoaderHelper.h"

#include "Logger/Logger.h"

using namespace carta;

LoaderHelper::LoaderHelper(std::shared_ptr<FileLoader> loader, std::shared_ptr<ImageStatus> status, std::mutex& image_mutex)
    : _loader(loader), _status(status), _image_mutex(image_mutex) {
    if (!_loader || !_status) {
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
        _status->CheckCurrentStokes(stokes);

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

// Image status parameters

casacore::IPosition LoaderHelper::OriginalImageShape() const {
    return _status->image_shape;
}

size_t LoaderHelper::Width() const {
    return _status->width;
}

size_t LoaderHelper::Height() const {
    return _status->height;
}

size_t LoaderHelper::Depth() const {
    return _status->depth;
}

size_t LoaderHelper::NumStokes() const {
    return _status->num_stokes;
}

int LoaderHelper::XAxis() const {
    return _status->x_axis;
}

int LoaderHelper::YAxis() const {
    return _status->y_axis;
}

int LoaderHelper::ZAxis() const {
    return _status->z_axis;
}

int LoaderHelper::SpectralAxis() const {
    return _status->spectral_axis;
}

int LoaderHelper::StokesAxis() const {
    return _status->stokes_axis;
}

int LoaderHelper::CurrentZ() const {
    return _status->z;
}

int LoaderHelper::CurrentStokes() const {
    return _status->stokes;
}
