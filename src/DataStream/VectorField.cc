/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <casacore/casa/BasicSL/Constants.h>

#include "Util/Message.h"
#include "VectorField.h"

namespace carta {

void GetTiles(int image_width, int image_height, int mip, std::vector<Tile>& tiles) {
    int tile_size_original = TILE_SIZE * mip;
    int num_tile_columns = ceil((double)image_width / tile_size_original);
    int num_tile_rows = ceil((double)image_height / tile_size_original);
    int32_t tile_layer = Tile::MipToLayer(mip, image_width, image_height, TILE_SIZE, TILE_SIZE);
    tiles.resize(num_tile_rows * num_tile_columns);

    for (int j = 0; j < num_tile_rows; ++j) {
        for (int i = 0; i < num_tile_columns; ++i) {
            tiles[j * num_tile_columns + i].x = i;
            tiles[j * num_tile_columns + i].y = j;
            tiles[j * num_tile_columns + i].layer = tile_layer;
        }
    }
}

void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
    std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality) {
    if (tile) {
        tile->set_x(x);
        tile->set_y(y);
        tile->set_layer(layer);
        tile->set_mip(mip);
        tile->set_width(tile_width);
        tile->set_height(tile_height);
        if (compression_type == CARTA::CompressionType::ZFP) {
            // Get and fill the NaN data
            auto nan_encodings = GetNanEncodingsBlock(array, 0, tile_width, tile_height);
            tile->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());
            // Compress and fill the data
            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(array, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            tile->set_image_data(compression_buffer.data(), compressed_size);
        } else {
            tile->set_image_data(array.data(), sizeof(float) * array.size());
        }
    }
}

CARTA::ImageBounds GetImageBounds(const Tile& tile, int image_width, int image_height, int mip) {
    int tile_size_original = TILE_SIZE * mip;
    CARTA::ImageBounds bounds;
    bounds.set_x_min(std::min(std::max(0, tile.x * tile_size_original), image_width));
    bounds.set_x_max(std::min(image_width, (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::min(std::max(0, tile.y * tile_size_original), image_height));
    bounds.set_y_max(std::min(image_height, (tile.y + 1) * tile_size_original));
    return bounds;
}

void ApplyThreshold(std::vector<float>& data, float threshold) {
    if (!std::isnan(threshold)) {
        for (auto& value : data) {
            if (!std::isnan(value) && (value < threshold)) {
                value = FLOAT_NAN;
            }
        }
    }
}

void CalculatePiPa(VectorFieldSettings& settings, std::vector<float>& current_stokes_data,
    std::unordered_map<std::string, std::vector<float>>& stokes_data, std::unordered_map<std::string, bool>& stokes_flag, const Tile& tile,
    int width, int height, int z_index, double progress, const std::function<void(CARTA::VectorOverlayTileData&)>& callback) {
    // Get vector field settings
    int file_id = settings.file_id;
    int mip = settings.smoothing_factor;
    bool fractional = settings.fractional;
    float threshold = (float)settings.threshold;
    CARTA::CompressionType compression_type = settings.compression_type;
    float compression_quality = settings.compression_quality;
    int stokes_intensity = settings.stokes_intensity;
    int stokes_angle = settings.stokes_angle;
    double q_error = settings.GetQError();
    double u_error = settings.GetUError();
    bool calculate_pi = settings.CalculatePi();
    bool calculate_pa = settings.CalculatePa();
    bool current_stokes_as_pi = settings.CurrentStokesAsPi();
    bool current_stokes_as_pa = settings.CurrentStokesAsPa();

    // Set response messages
    auto response = Message::VectorOverlayTileData(file_id, z_index, stokes_intensity, stokes_angle, compression_type, compression_quality);
    auto* tile_pi = response.add_intensity_tiles();
    auto* tile_pa = response.add_angle_tiles();

    // Current stokes data as PI or PA
    if ((current_stokes_as_pi || current_stokes_as_pa) && !current_stokes_data.empty()) {
        // Apply a threshold cut
        ApplyThreshold(current_stokes_data, threshold);

        if (current_stokes_as_pi) {
            FillTileData(
                tile_pi, tile.x, tile.y, tile.layer, mip, width, height, current_stokes_data, compression_type, compression_quality);
        }

        if (current_stokes_as_pa) {
            FillTileData(
                tile_pa, tile.x, tile.y, tile.layer, mip, width, height, current_stokes_data, compression_type, compression_quality);
        }
    }

    // Calculate PI and PA using stokes data I, Q or U
    // Lambda function to apply a threshold
    auto apply_threshold = [&](float i, float result) {
        return ((std::isnan(i) || (!std::isnan(threshold) && (i < threshold))) ? FLOAT_NAN : result);
    };

    if (calculate_pi) {
        std::vector<float> pi;
        pi.resize(width * height);

        // Lambda function to calculate PI, errors are applied
        auto calc_pi = [&](float q, float u) {
            if (!std::isnan(q) && !std::isnan(u)) {
                return ((float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - (std::pow(q_error, 2) + std::pow(u_error, 2)) / 2.0));
            }
            return FLOAT_NAN;
        };

        std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pi.begin(), calc_pi);
        if (fractional) { // Calculate fractional PI
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), CalcFpi);
        }

        if (stokes_flag["I"]) { // Set NAN for PI/FPI if stokes I is NAN or below the threshold
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), apply_threshold);
        }
        FillTileData(tile_pi, tile.x, tile.y, tile.layer, mip, width, height, pi, compression_type, compression_quality);
    }

    if (calculate_pa) {
        std::vector<float> pa;
        pa.resize(width * height);
        std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pa.begin(), CalcPa);

        if (stokes_flag["I"]) { // Set NAN for PA if stokes I is NAN or below the threshold
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pa.begin(), pa.begin(), apply_threshold);
        }
        FillTileData(tile_pa, tile.x, tile.y, tile.layer, mip, width, height, pa, compression_type, compression_quality);
    }

    // Send response message
    response.set_progress(progress);
    callback(response);
}

bool Valid(float a, float b) {
    return (!std::isnan(a) && !std::isnan(b));
}

float CalcFpi(float i, float pi) {
    return (Valid(i, pi) ? (float)100.0 * (pi / i) : FLOAT_NAN);
}

float CalcPa(float q, float u) {
    return (Valid(q, u) ? ((float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2) : FLOAT_NAN);
}

} // namespace carta
