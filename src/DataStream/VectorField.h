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

#define FLOAT_NAN std::numeric_limits<float>::quiet_NaN()

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

    // Extra variables to be determined based on the existence of stokes axis
    bool calculate_pi;
    bool calculate_pa;
    bool current_stokes_as_pi;
    bool current_stokes_as_pa;

    VectorFieldSettings() : calculate_pi(false), calculate_pa(false), current_stokes_as_pi(false), current_stokes_as_pa(false) {
        ClearSettings();
    }

    VectorFieldSettings(const CARTA::SetVectorOverlayParameters& message, int stokes_axis = -1) {
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

        calculate_pi = stokes_intensity == 1 && stokes_axis > -1;
        calculate_pa = stokes_angle == 1 && stokes_axis > -1;
        current_stokes_as_pi = (stokes_intensity == 0 && stokes_axis > -1) || stokes_axis < 0;
        current_stokes_as_pa = (stokes_angle == 0 && stokes_axis > -1) || stokes_axis < 0;
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
void ApplyThreshold(std::vector<float>& data, float threshold);

// Functions to calculate fractional PI and PA
void CalculatePiPa(const VectorFieldSettings& settings, std::vector<float>& current_stokes_data,
    std::unordered_map<std::string, std::vector<float>>& stokes_data, std::unordered_map<std::string, bool>& stokes_flag, const Tile& tile,
    int width, int height, int z_index, double progress, const std::function<void(CARTA::VectorOverlayTileData&)>& callback);
bool Valid(float a, float b);
float CalcFpi(float i, float pi);
float CalcPa(float q, float u);

} // namespace carta

#endif // CARTA_BACKEND__FRAME_VECTORFIELD_H_
