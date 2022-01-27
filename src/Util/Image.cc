/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Image.h"

int GetStokesValue(const CARTA::PolarizationType& stokes_type) {
    int stokes_value(-1);
    if (StokesValues.count(stokes_type)) {
        stokes_value = StokesValues[stokes_type];
    }
    return stokes_value;
}

CARTA::PolarizationType GetStokesType(int stokes_value) {
    CARTA::PolarizationType stokes_type = CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
    if (StokesTypes.count(stokes_value)) {
        stokes_type = StokesTypes[stokes_value];
    }
    return stokes_type;
}

void GetTiles(int image_width, int image_height, int mip, std::vector<Tile>& tiles) {
    int num_tile_columns = ceil((double)image_width / mip);
    int num_tile_rows = ceil((double)image_height / mip);
    int32_t tile_layer = Tile::MipToLayer(mip, image_width, image_height, TILE_SIZE, TILE_SIZE);
    tiles.resize(num_tile_rows * num_tile_columns);

    for (int i = 0; i < num_tile_columns; ++i) {
        for (int j = 0; j < num_tile_rows; ++j) {
            tiles[j * num_tile_columns + i].x = i;
            tiles[j * num_tile_columns + i].y = j;
            tiles[j * num_tile_columns + i].layer = tile_layer;
        }
    }
}

CARTA::ImageBounds GetImageBounds(const Tile& tile, int image_width, int image_height, int mip) {
    CARTA::ImageBounds bounds;
    int tile_size_original = TILE_SIZE * mip;
    bounds.set_x_min(std::min(std::max(0, tile.x * tile_size_original), image_width));
    bounds.set_x_max(std::min(image_width, (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::min(std::max(0, tile.y * tile_size_original), image_height));
    bounds.set_y_max(std::min(image_height, (tile.y + 1) * tile_size_original));
    return bounds;
}
