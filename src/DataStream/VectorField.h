/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_VECTORFIELD_H_
#define CARTA_BACKEND__FRAME_VECTORFIELD_H_

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>

#include "DataStream/Compression.h"
#include "DataStream/Tile.h"
#include "Util/Image.h"

namespace carta {

struct VectorFieldSettings {
    int file_id;
    int smoothing_factor;
    bool fractional;
    double threshold;
    bool debiasing;
    double q_error;
    double u_error;
    int stokes_intensity;
    int stokes_angle;
    CARTA::CompressionType compression_type;
    float compression_quality;

    VectorFieldSettings() {
        ClearSettings();
    }

    VectorFieldSettings(const CARTA::SetVectorOverlayParameters& message) {
        file_id = (int)message.file_id();
        smoothing_factor = (int)message.smoothing_factor();
        fractional = message.fractional();
        threshold = message.threshold();
        debiasing = message.debiasing();
        q_error = message.q_error();
        u_error = message.u_error();
        stokes_intensity = message.stokes_intensity();
        stokes_angle = message.stokes_angle();
        compression_type = message.compression_type();
        compression_quality = message.compression_quality();
    }

    // Equality operator for checking if vector field settings have changed
    bool operator==(const VectorFieldSettings& rhs) const {
        return (file_id == rhs.file_id && smoothing_factor == rhs.smoothing_factor && fractional == rhs.fractional &&
                threshold == rhs.threshold && debiasing == rhs.debiasing && q_error == rhs.q_error && u_error == rhs.u_error &&
                stokes_intensity == rhs.stokes_intensity && stokes_angle == rhs.stokes_angle && compression_type == rhs.compression_type &&
                compression_quality == rhs.compression_quality);
    }

    bool operator!=(const VectorFieldSettings& rhs) const {
        return !(*this == rhs);
    }

    void ClearSettings() {
        file_id = -1;
        smoothing_factor = 0;
        fractional = false;
        threshold = std::numeric_limits<double>::quiet_NaN();
        debiasing = false;
        q_error = 0;
        u_error = 0;
        stokes_intensity = -1;
        stokes_angle = -1;
        compression_type = CARTA::CompressionType::NONE;
        compression_quality = 0;
    }
};

void GetTiles(int image_width, int image_height, int mip, std::vector<carta::Tile>& tiles);
void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
    std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);
CARTA::ImageBounds GetImageBounds(const carta::Tile& tile, int image_width, int image_height, int mip);

} // namespace carta

#endif // CARTA_BACKEND__FRAME_VECTORFIELD_H_
