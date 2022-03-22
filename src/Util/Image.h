/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_IMAGE_H_
#define CARTA_BACKEND__UTIL_IMAGE_H_

#include <cmath>
#include <functional>
#include <string>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay_tile.pb.h>

#include "DataStream/Tile.h"

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0
#define ALL_REGIONS -10
#define TEMP_REGION_ID -100

// x axis
#define ALL_X -2

// y axis
#define ALL_Y -2

// z axis
#define DEFAULT_Z 0
#define CURRENT_Z -1
#define ALL_Z -2

// stokes
#define DEFAULT_STOKES 0
#define CURRENT_STOKES -1

// raster image data
#define TILE_SIZE 256
#define CHUNK_SIZE 512

// histograms
#define AUTO_BIN_SIZE -1

// z profile calculation
#define INIT_DELTA_Z 10
#define TARGET_DELTA_TIME 50 // milliseconds
#define TARGET_PARTIAL_CURSOR_TIME 500
#define TARGET_PARTIAL_REGION_TIME 1000

// AxisRange() defines the full axis ALL_Z
// AxisRange(0) defines a single axis index, 0, in this example
// AxisRange(0, 1) defines the axis range including [0, 1] in this example
// AxisRange(0, 2) defines the axis range including [0, 1, 2] in this example

struct AxisRange {
    int from, to;
    AxisRange() {
        from = 0;
        to = ALL_Z;
    }
    AxisRange(int from_and_to_) {
        from = to = from_and_to_;
    }
    AxisRange(int from_, int to_) {
        from = from_;
        to = to_;
    }
};

struct PointXy {
    // Utilities for cursor and point regions
    float x, y;

    PointXy() {
        x = -1.0;
        y = -1.0;
    }
    PointXy(float x_, float y_) {
        x = x_;
        y = y_;
    }
    void operator=(const PointXy& other) {
        x = other.x;
        y = other.y;
    }
    bool operator==(const PointXy& rhs) const {
        if ((x != rhs.x) || (y != rhs.y)) {
            return false;
        }
        return true;
    }
    void ToIndex(int& x_index, int& y_index) {
        // convert float to int for index into image data array
        x_index = static_cast<int>(std::round(x));
        y_index = static_cast<int>(std::round(y));
    }
    bool InImage(int xrange, int yrange) {
        // returns whether x, y are within given image axis ranges
        int x_index, y_index;
        ToIndex(x_index, y_index);
        bool x_in_image = (x_index >= 0) && (x_index < xrange);
        bool y_in_image = (y_index >= 0) && (y_index < yrange);
        return (x_in_image && y_in_image);
    }
};

// stokes types and value conversion
static std::unordered_map<CARTA::PolarizationType, int> StokesValues{{CARTA::PolarizationType::I, 1}, {CARTA::PolarizationType::Q, 2},
    {CARTA::PolarizationType::U, 3}, {CARTA::PolarizationType::V, 4}, {CARTA::PolarizationType::RR, 5}, {CARTA::PolarizationType::LL, 6},
    {CARTA::PolarizationType::RL, 7}, {CARTA::PolarizationType::LR, 8}, {CARTA::PolarizationType::XX, 9}, {CARTA::PolarizationType::YY, 10},
    {CARTA::PolarizationType::XY, 11}, {CARTA::PolarizationType::YX, 12}};

static std::unordered_map<int, CARTA::PolarizationType> StokesTypes{{1, CARTA::PolarizationType::I}, {2, CARTA::PolarizationType::Q},
    {3, CARTA::PolarizationType::U}, {4, CARTA::PolarizationType::V}, {5, CARTA::PolarizationType::RR}, {6, CARTA::PolarizationType::LL},
    {7, CARTA::PolarizationType::RL}, {8, CARTA::PolarizationType::LR}, {9, CARTA::PolarizationType::XX}, {10, CARTA::PolarizationType::YY},
    {11, CARTA::PolarizationType::XY}, {12, CARTA::PolarizationType::YX}};

static std::unordered_map<std::string, CARTA::PolarizationType> StokesStringTypes{{"I", CARTA::PolarizationType::I},
    {"Q", CARTA::PolarizationType::Q}, {"U", CARTA::PolarizationType::U}, {"V", CARTA::PolarizationType::V},
    {"RR", CARTA::PolarizationType::RR}, {"LL", CARTA::PolarizationType::LL}, {"RL", CARTA::PolarizationType::RL},
    {"LR", CARTA::PolarizationType::LR}, {"XX", CARTA::PolarizationType::XX}, {"YY", CARTA::PolarizationType::YY},
    {"XY", CARTA::PolarizationType::XY}, {"YX", CARTA::PolarizationType::YX}};

int GetStokesValue(const CARTA::PolarizationType& stokes_type);
CARTA::PolarizationType GetStokesType(int stokes_value);

struct VectorFieldSettings {
    int smoothing_factor = 0; // Initialize as 0 which represents the empty setting
    bool fractional;
    double threshold;
    bool debiasing;
    double q_error;
    double u_error;
    int stokes_intensity;
    int stokes_angle;
    CARTA::CompressionType compression_type;
    float compression_quality;

    // Equality operator for checking if vector field settings have changed
    bool operator==(const VectorFieldSettings& rhs) const {
        return (this->smoothing_factor == rhs.smoothing_factor && this->fractional == rhs.fractional && this->threshold == rhs.threshold &&
                this->debiasing == rhs.debiasing && this->q_error == rhs.q_error && this->u_error == rhs.u_error &&
                this->stokes_intensity == rhs.stokes_intensity && this->stokes_angle == rhs.stokes_angle &&
                this->compression_type == rhs.compression_type && this->compression_quality == rhs.compression_quality);
    }

    bool operator!=(const VectorFieldSettings& rhs) const {
        return !(*this == rhs);
    }
};

// Polarization vector field callback (intensity_tile, angle_tile, progress)
using VectorFieldCallback = const std::function<void(CARTA::VectorOverlayTileData&)>;

void GetTiles(int image_width, int image_height, int mip, std::vector<carta::Tile>& tiles);
CARTA::ImageBounds GetImageBounds(const carta::Tile& tile, int image_width, int image_height, int mip);

#endif // CARTA_BACKEND__UTIL_IMAGE_H_
