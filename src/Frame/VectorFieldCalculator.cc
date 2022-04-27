/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "VectorFieldCalculator.h"

#include "DataStream/Compression.h"

#define FLOAT_NAN std::numeric_limits<float>::quiet_NaN()

namespace carta {

bool VectorFieldCalculator::DoCalculations(VectorFieldCallback& callback) {
    if (!(frame && frame->IsValid())) {
        return false;
    }

    // Prevent deleting the Frame while the loop is not finished yet
    std::shared_lock lock(frame->GetActiveTaskMutex());

    // Get vector field settings
    auto vector_field_settings = frame->GetVectorFieldParameters();
    int mip = vector_field_settings.smoothing_factor;
    bool fractional = vector_field_settings.fractional;
    float threshold = (float)vector_field_settings.threshold;
    CARTA::CompressionType compression_type = vector_field_settings.compression_type;
    float compression_quality = vector_field_settings.compression_quality;
    int stokes_intensity = vector_field_settings.stokes_intensity;
    int stokes_angle = vector_field_settings.stokes_angle;
    double q_error = vector_field_settings.q_error;
    double u_error = vector_field_settings.u_error;
    bool debiasing = vector_field_settings.debiasing;
    if (!debiasing) {
        q_error = u_error = 0;
    }

    // Get current channel
    int channel = frame->CurrentZ();

    // Set response messages
    CARTA::VectorOverlayTileData response;
    response.set_channel(channel);
    response.set_stokes_intensity(stokes_intensity);
    response.set_stokes_angle(stokes_angle);
    response.set_compression_type(compression_type);
    response.set_compression_quality(compression_quality);
    auto* tile_pi = response.add_intensity_tiles();
    auto* tile_pa = response.add_angle_tiles();

    if ((stokes_intensity < 0) && (stokes_angle < 0)) { // Clear the overlay requirements
        frame->ClearVectorFieldParameters();
        response.set_progress(1.0);
        callback(response);
        return true;
    }

    // Get tiles
    std::vector<Tile> tiles;
    int image_width = frame->Width();
    int image_height = frame->Height();
    GetTiles(image_width, image_height, mip, tiles);

    auto apply_threshold_on_data = [&](std::vector<float>& tmp_data) {
        if (!std::isnan(threshold)) {
            for (auto& value : tmp_data) {
                if (!std::isnan(value) && (value < threshold)) {
                    value = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }
    };

    // Consider the case if an image has no stokes axis
    if (frame->StokesAxis() < 0) {
        // Get image tiles data
        for (int i = 0; i < tiles.size(); ++i) {
            auto& tile = tiles[i];
            auto bounds = GetImageBounds(tile, image_width, image_height, mip);

            // Get downsampled 2D pixel data
            std::vector<float> downsampled_data;
            int downsampled_width, downsampled_height;
            if (!frame->GetDownsampledRasterData(
                    downsampled_data, downsampled_width, downsampled_height, channel, CURRENT_STOKES, bounds, mip)) {
                return false;
            }
            // Apply a threshold cut
            apply_threshold_on_data(downsampled_data);

            if (stokes_angle > -1) {
                // Fill PA tiles protobuf data
                FillTileData(tile_pa, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, downsampled_data,
                    compression_type, compression_quality);
            }
            if (stokes_intensity > -1) {
                // Fill PI tiles protobuf data
                FillTileData(tile_pi, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, downsampled_data,
                    compression_type, compression_quality);
            }

            // Send partial results to the frontend
            double progress = (double)(i + 1) / tiles.size();
            response.set_progress(progress);
            callback(response);
        }
        return true;
    }

    // Consider the case if an image has stokes axis

    // Get Stokes I, Q, and U indices
    int stokes_i, stokes_q, stokes_u;
    bool require_stokes_i(fractional || !std::isnan(threshold));
    bool calculate_pi(stokes_intensity == 1), calculate_pa(stokes_angle == 1);
    bool current_stokes_as_pi(stokes_intensity == 0), current_stokes_as_pa(stokes_angle == 0);

    // Calculating fractional PI or applying threshold require stokes I
    if (require_stokes_i && !frame->GetStokesTypeIndex("Ix", stokes_i)) {
        return false;
    }

    // Calculating PI or PA require stokes Q and U data
    if (calculate_pi || calculate_pa) {
        if (!frame->GetStokesTypeIndex("Qx", stokes_q) || !frame->GetStokesTypeIndex("Ux", stokes_u)) {
            return false;
        }
    }

    // Get image tiles data
    for (int i = 0; i < tiles.size(); ++i) {
        auto& tile = tiles[i];
        auto bounds = GetImageBounds(tile, image_width, image_height, mip);

        // Get downsampled raster tile data
        std::vector<float> downsampled_i, downsampled_q, downsampled_u, downsampled_data;
        int downsampled_width, downsampled_height;

        // Calculating fractional PI or applying threshold require stokes I
        if (require_stokes_i &&
            !frame->GetDownsampledRasterData(downsampled_i, downsampled_width, downsampled_height, channel, stokes_i, bounds, mip)) {
            return false;
        }

        // Calculating PI or PA require stokes Q and U data
        if (calculate_pi || calculate_pa) {
            if (!frame->GetDownsampledRasterData(downsampled_q, downsampled_width, downsampled_height, channel, stokes_q, bounds, mip) ||
                !frame->GetDownsampledRasterData(downsampled_u, downsampled_width, downsampled_height, channel, stokes_u, bounds, mip)) {
                return false;
            }
        }

        // Calculate the current stokes as polarized intensity or polarized angle
        if (current_stokes_as_pi || current_stokes_as_pa) {
            if (!frame->GetDownsampledRasterData(
                    downsampled_data, downsampled_width, downsampled_height, channel, CURRENT_STOKES, bounds, mip)) {
                return false;
            }
            // Apply a threshold cut
            apply_threshold_on_data(downsampled_data);
        }

        // Lambda functions for calculating PI, fractional PI, and PA
        auto is_valid = [](float a, float b) { return (!std::isnan(a) && !std::isnan(b)); };

        auto calc_pi = [&](float q, float u) {
            return (is_valid(q, u)
                        ? ((float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - (std::pow(q_error, 2) + std::pow(u_error, 2)) / 2.0))
                        : FLOAT_NAN);
        };

        auto calc_fpi = [&](float i, float pi) { return (is_valid(i, pi) ? (pi / i) : FLOAT_NAN); };

        auto calc_pa = [&](float q, float u) {
            return (is_valid(q, u) ? ((float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2) : FLOAT_NAN);
        };

        auto apply_threshold = [&](float i, float result) {
            return ((std::isnan(i) || (!std::isnan(threshold) && (i < threshold))) ? FLOAT_NAN : result);
        };

        // Calculate polarized intensity (pi) or polarized angle (pa) if required
        std::vector<float> pi, pa;

        if (calculate_pi) {
            pi.resize(downsampled_width * downsampled_height);
            std::transform(downsampled_q.begin(), downsampled_q.end(), downsampled_u.begin(), pi.begin(), calc_pi);
            if (fractional) { // Calculate fractional PI
                std::transform(downsampled_i.begin(), downsampled_i.end(), pi.begin(), pi.begin(), calc_fpi);
            }
        }

        if (calculate_pa) {
            pa.resize(downsampled_width * downsampled_height);
            std::transform(downsampled_q.begin(), downsampled_q.end(), downsampled_u.begin(), pa.begin(), calc_pa);
        }

        // Set NAN for PI/FPI if stokes I is NAN or below the threshold
        if (calculate_pi && require_stokes_i) {
            std::transform(downsampled_i.begin(), downsampled_i.end(), pi.begin(), pi.begin(), apply_threshold);
        }

        // Set NAN for PA if stokes I is NAN or below the threshold
        if (calculate_pa && require_stokes_i) {
            std::transform(downsampled_i.begin(), downsampled_i.end(), pa.begin(), pa.begin(), apply_threshold);
        }

        // Fill polarized intensity tiles protobuf data
        if (calculate_pi) { // Send PI as polarized intensity
            FillTileData(tile_pi, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, pi, compression_type,
                compression_quality);
        }
        if (current_stokes_as_pi) { // Send current stokes data as polarized intensity
            FillTileData(tile_pi, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, downsampled_data,
                compression_type, compression_quality);
        }

        // Fill polarized angle tiles protobuf data
        if (calculate_pa) { // Send PA as polarized angle
            FillTileData(tile_pa, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, pa, compression_type,
                compression_quality);
        }
        if (current_stokes_as_pa) { // Send current stokes data as polarized angle
            FillTileData(tile_pa, tiles[i].x, tiles[i].y, tiles[i].layer, mip, downsampled_width, downsampled_height, downsampled_data,
                compression_type, compression_quality);
        }

        // Send partial results to the frontend
        double progress = (double)(i + 1) / tiles.size();
        response.set_progress(progress);
        callback(response);
    }
    return true;
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
