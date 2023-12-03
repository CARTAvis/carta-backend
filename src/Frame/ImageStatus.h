/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_FRAME_IMAGESTATUS_H_
#define CARTA_SRC_FRAME_IMAGESTATUS_H_

#include "ImageData/FileLoader.h"

#include <memory>
#include <string>

#include <casacore/casa/Arrays/IPosition.h>

namespace carta {

struct ImageStatus {
    // Image shape or sizes
    casacore::IPosition image_shape;
    size_t width;
    size_t height;
    size_t depth;
    size_t num_stokes;

    // X and Y are render axes, Z is depth axis (non-render axis) that is not stokes (if any)
    int x_axis;
    int y_axis;
    int z_axis;

    int spectral_axis;
    int stokes_axis;

    int z;      // Current channel
    int stokes; // Current stokes

    bool valid;

    explicit ImageStatus(uint32_t session_id, std::shared_ptr<FileLoader> loader, int default_z, std::string& error);

    void SetCurrentZ(int z_);
    void SetCurrentStokes(int stokes_);

    void CheckCurrentZ(int& z_) const;
    void CheckCurrentStokes(int& stokes_) const;

    bool IsCurrentChannel(int& z_, int& stokes_) const;
};

} // namespace carta

#endif // CARTA_SRC_FRAME_IMAGESTATUS_H_
