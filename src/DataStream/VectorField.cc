/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "VectorField.h"
#include "Util/Message.h"

namespace carta {

VectorField::VectorField() {
    _sets.ClearSettings();
}

bool VectorField::SetParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis) {
    VectorFieldSettings new_sets(message, stokes_axis);
    if (_sets != new_sets) {
        _sets = new_sets;
        return true;
    }
    return false;
}

bool VectorField::ClearSets(const std::function<void(CARTA::VectorOverlayTileData&)>& callback, int z_index) {
    auto& settings = _sets;
    if (settings.smoothing_factor < 1) {
        return true;
    }

    if (settings.stokes_intensity < 0 && settings.stokes_angle < 0) {
        _sets.ClearSettings();
        auto empty_response = Message::VectorOverlayTileData(settings.file_id, z_index, settings.stokes_intensity, settings.stokes_angle,
            settings.compression_type, settings.compression_quality);
        empty_response.set_progress(1.0);
        callback(empty_response);
        return true;
    }

    return false;
}

void VectorField::CalculatePiPa(std::unordered_map<std::string, std::vector<float>>& stokes_data,
    std::unordered_map<std::string, bool>& stokes_flag, const Tile& tile, int width, int height, int z_index, double progress,
    const std::function<void(CARTA::VectorOverlayTileData&)>& callback) {
    // Set response messages
    auto response = Message::VectorOverlayTileData(
        _sets.file_id, z_index, _sets.stokes_intensity, _sets.stokes_angle, _sets.compression_type, _sets.compression_quality);
    auto* tile_pi = response.add_intensity_tiles();
    auto* tile_pa = response.add_angle_tiles();

    // Threshold cut operator to be applied
    ThresholdCut threshold_cut(_sets.threshold);

    // Current stokes data as PI or PA
    if (_sets.current_stokes_as_pi || _sets.current_stokes_as_pa) {
        // Apply a threshold cut
        std::for_each(stokes_data["CUR"].begin(), stokes_data["CUR"].end(), threshold_cut);

        if (_sets.current_stokes_as_pi) {
            FillTileData(tile_pi, tile.x, tile.y, tile.layer, _sets.smoothing_factor, width, height, stokes_data["CUR"],
                _sets.compression_type, _sets.compression_quality);
        }
        if (_sets.current_stokes_as_pa) {
            FillTileData(tile_pa, tile.x, tile.y, tile.layer, _sets.smoothing_factor, width, height, stokes_data["CUR"],
                _sets.compression_type, _sets.compression_quality);
        }
    }

    // Calculate PI and PA using stokes data I, Q or U

    if (_sets.calculate_pi) {
        std::vector<float> pi;
        pi.resize(width * height);

        // Lambda function to calculate PI, errors are applied
        CalcPi calc_pi(_sets.q_error, _sets.u_error);

        std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pi.begin(), calc_pi);
        if (_sets.fractional) { // Calculate fractional PI
            CalcFpi calc_fpi;
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), calc_fpi);
        }

        if (stokes_flag["I"]) { // Set NAN for PI/FPI if stokes I is NAN or below the threshold
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), threshold_cut);
        }
        FillTileData(tile_pi, tile.x, tile.y, tile.layer, _sets.smoothing_factor, width, height, pi, _sets.compression_type,
            _sets.compression_quality);
    }

    if (_sets.calculate_pa) {
        std::vector<float> pa;
        pa.resize(width * height);
        CalcPa calc_pa;
        std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pa.begin(), calc_pa);

        if (stokes_flag["I"]) { // Set NAN for PA if stokes I is NAN or below the threshold
            std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pa.begin(), pa.begin(), threshold_cut);
        }
        FillTileData(tile_pa, tile.x, tile.y, tile.layer, _sets.smoothing_factor, width, height, pa, _sets.compression_type,
            _sets.compression_quality);
    }

    // Send response message
    response.set_progress(progress);
    callback(response);
}

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

} // namespace carta
