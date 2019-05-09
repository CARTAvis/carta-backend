#ifndef CARTA_BACKEND__TILE_H_
#define CARTA_BACKEND__TILE_H_

#include <cstdint>
#include <iostream>

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
};

#endif // CARTA_BACKEND__TILE_H_
