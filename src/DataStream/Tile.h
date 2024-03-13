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

struct Tile {
    int32_t x;
    int32_t y;
    int32_t layer;

    static int32_t Encode(int32_t x, int32_t y, int32_t layer) {
        int32_t layer_width = 1 << layer;
        if (x < 0 || y < 0 || layer < 0 || layer > 12 || x >= layer_width || y >= layer_width) {
            return -1;
        }

        return ((layer << 24) | (y << 12) | x);
    }

    static Tile Decode(int32_t encoded_value) {
        int32_t x = (((encoded_value << 19) >> 19) + 4096) % 4096;
        int32_t layer = ((encoded_value >> 24) + 128) % 128;
        int32_t y = (((encoded_value << 7) >> 19) + 4096) % 4096;
        return Tile{x, y, layer};
    }

    static int32_t LayerToMip(int32_t layer, int32_t image_width, int32_t image_height, int32_t tile_width, int32_t tile_height) {
        double total_tiles_x = ceil((double)(image_width) / tile_width);
        double total_tiles_y = ceil((double)(image_height) / tile_height);
        double max_mip = std::max(total_tiles_x, total_tiles_y);
        double total_layers = ceil(log2(max_mip));
        return pow(2.0, total_layers - layer);
    }

    static int32_t MipToLayer(int32_t mip, int32_t image_width, int32_t image_height, int32_t tile_width, int32_t tile_height) {
        double total_tiles_x = ceil((double)(image_width) / tile_width);
        double total_tiles_y = ceil((double)(image_height) / tile_height);
        double max_mip = std::max(total_tiles_x, total_tiles_y);
        return ceil(log2(max_mip / mip));
    }
};

} // namespace carta

#endif // CARTA_SRC_DATASTREAM_TILE_H_
