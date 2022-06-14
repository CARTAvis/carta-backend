/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_VECTORFIELDCALCULATOR_H_
#define CARTA_BACKEND__FRAME_VECTORFIELDCALCULATOR_H_

#include "Frame.h"

namespace carta {

using VectorFieldCallback = const std::function<void(CARTA::VectorOverlayTileData&)>;

struct VectorFieldCalculator {
    std::shared_ptr<Frame> frame;

    VectorFieldCalculator(const std::shared_ptr<Frame>& frame_) : frame(frame_) {}

    bool DoCalculations(VectorFieldCallback& callback);
};

void GetTiles(int image_width, int image_height, int mip, std::vector<carta::Tile>& tiles);
void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
    std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);
CARTA::ImageBounds GetImageBounds(const carta::Tile& tile, int image_width, int image_height, int mip);

} // namespace carta

#endif // CARTA_BACKEND__FRAME_VECTORFIELDCALCULATOR_H_
