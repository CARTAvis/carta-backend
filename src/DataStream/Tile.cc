/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

/**
 * @file Tile.cc
 * @brief Provides the Tile struct for encoding and decoding spatial tile coordinates and mipmap layer logic.
 *
 * This file defines the Tile struct, which represents a tile in a multi-resolution tiling system.
 * It includes utility functions for encoding and decoding tile data into a compact 32-bit format,
 * and for converting between layer and mipmap.
 */

#include "Tile.h"
