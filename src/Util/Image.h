/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_IMAGE_H_
#define CARTA_BACKEND__UTIL_IMAGE_H_

#include <cmath>
#include <string>

#include <carta-protobuf/enums.pb.h>

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0
#define ALL_REGIONS -10

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
int GetStokesValue(const CARTA::StokesType& stokes_type);
CARTA::StokesType GetStokesType(int stokes_value);

#endif // CARTA_BACKEND__UTIL_IMAGE_H_
