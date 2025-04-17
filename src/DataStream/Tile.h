/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_DATASTREAM_TILE_H_
#define CARTA_SRC_DATASTREAM_TILE_H_

#include <cmath>
#include <cstdint>
#include <iostream>

namespace carta {
/**
 * @struct Tile
 * @brief Represents a tile in a multi-resolution grid with spatial coordinates and a resolution layer.
 *
 * The Tile struct contains 2D spatial coordinates (x, y) and a resolution layer. It provides
 * methods to encode/decode tile information into/from a 32-bit integer, and convert
 * between resolution layers and mipmap levels.
 */
struct Tile {
    int32_t x;     /**< The X coordinate of the tile. */
    int32_t y;     /**< The Y coordinate of the tile. */
    int32_t layer; /**< The resolution layer of the tile. */

    /**
     * @brief Encodes tile coordinates and layer into a 32-bit integer.
     *
     * @param x The X coordinate (must be within bounds of the layer).
     * @param y The Y coordinate (must be within bounds of the layer).
     * @param layer The layer index (must be between 0 and 12).
     * @return Encoded 32-bit integer value, or -1 if input is invalid.
     */
    static int32_t Encode(int32_t x, int32_t y, int32_t layer) {
        int32_t layer_width = 1 << layer;
        if (x < 0 || y < 0 || layer < 0 || layer > 12 || x >= layer_width || y >= layer_width) {
            return -1;
        }

        return ((layer << 24) | (y << 12) | x);
    }

    /**
     * @brief Decodes a 32-bit integer into a Tile object.
     *
     * @param encoded_value The encoded 32-bit integer.
     * @return A Tile object with decoded x, y, and layer values.
     */
    static Tile Decode(int32_t encoded_value) {
        int32_t x = encoded_value & 0xFFF;
        int32_t y = (encoded_value >> 12) & 0xFFF;
        int32_t layer = (encoded_value >> 24) & 0xFF;
        return Tile{x, y, layer};
    }

    /**
     * @brief Converts a layer index to the corresponding mipmap size.
     *
     * @param layer The layer index to convert.
     * @param image_width The width of the full-resolution image.
     * @param image_height The height of the full-resolution image.
     * @param tile_width The width of a tile in pixels.
     * @param tile_height The height of a tile in pixels.
     * @return The mip size corresponding to the given layer.
     */
    static int32_t LayerToMip(int32_t layer, int32_t image_width, int32_t image_height, int32_t tile_width, int32_t tile_height) {
        double total_tiles_x = ceil((double)(image_width) / tile_width);
        double total_tiles_y = ceil((double)(image_height) / tile_height);
        double max_mip = std::max(total_tiles_x, total_tiles_y);
        double total_layers = ceil(log2(max_mip));
        return pow(2.0, total_layers - layer);
    }

    /**
     * @brief Converts a mipmap size to the corresponding layer index.
     *
     * @param mip The mipmap size to convert.
     * @param image_width The width of the full-resolution image.
     * @param image_height The height of the full-resolution image.
     * @param tile_width The width of a tile in pixels.
     * @param tile_height The height of a tile in pixels.
     * @return The corresponding layer index, or -1 if invalid.
     */
    static int32_t MipToLayer(int32_t mip, int32_t image_width, int32_t image_height, int32_t tile_width, int32_t tile_height) {
        if (mip <= 0)
            return -1; // invalid mip

        double total_tiles_x = ceil((double)(image_width) / tile_width);
        double total_tiles_y = ceil((double)(image_height) / tile_height);
        double max_mip = std::max(total_tiles_x, total_tiles_y);

        if (mip > max_mip)
            return -1;

        return ceil(log2(max_mip / mip));
    }
};

} // namespace carta

#endif // CARTA_SRC_DATASTREAM_TILE_H_
