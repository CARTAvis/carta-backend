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
#define COMPUTE_STOKES_PTOTAL 13   // Polarized intensity ((Q^2+U^2+V^2)^(1/2))
#define COMPUTE_STOKES_PLINEAR 14  // Linearly Polarized intensity ((Q^2+U^2)^(1/2))
#define COMPUTE_STOKES_PFTOTAL 15  // Polarization Fraction (Ptotal/I)
#define COMPUTE_STOKES_PFLINEAR 16 // Linear Polarization Fraction (Plinear/I)
#define COMPUTE_STOKES_PANGLE 17   // Linear Polarization Angle (0.5 arctan(U/Q)) (in radians)

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
    bool operator!=(const AxisRange& rhs) const {
        if ((from != rhs.from) || (to != rhs.to)) {
            return true;
        }
        return false;
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
    {CARTA::PolarizationType::XY, 11}, {CARTA::PolarizationType::YX, 12}, {CARTA::PolarizationType::Ptotal, 13},
    {CARTA::PolarizationType::Plinear, 14}, {CARTA::PolarizationType::PFtotal, 15}, {CARTA::PolarizationType::PFlinear, 16},
    {CARTA::PolarizationType::Pangle, 17}};

static std::unordered_map<int, CARTA::PolarizationType> StokesTypes{{1, CARTA::PolarizationType::I}, {2, CARTA::PolarizationType::Q},
    {3, CARTA::PolarizationType::U}, {4, CARTA::PolarizationType::V}, {5, CARTA::PolarizationType::RR}, {6, CARTA::PolarizationType::LL},
    {7, CARTA::PolarizationType::RL}, {8, CARTA::PolarizationType::LR}, {9, CARTA::PolarizationType::XX}, {10, CARTA::PolarizationType::YY},
    {11, CARTA::PolarizationType::XY}, {12, CARTA::PolarizationType::YX}, {13, CARTA::PolarizationType::Ptotal},
    {14, CARTA::PolarizationType::Plinear}, {15, CARTA::PolarizationType::PFtotal}, {16, CARTA::PolarizationType::PFlinear},
    {17, CARTA::PolarizationType::Pangle}};

static std::unordered_map<std::string, CARTA::PolarizationType> StokesStringTypes{{"I", CARTA::PolarizationType::I},
    {"Q", CARTA::PolarizationType::Q}, {"U", CARTA::PolarizationType::U}, {"V", CARTA::PolarizationType::V},
    {"RR", CARTA::PolarizationType::RR}, {"LL", CARTA::PolarizationType::LL}, {"RL", CARTA::PolarizationType::RL},
    {"LR", CARTA::PolarizationType::LR}, {"XX", CARTA::PolarizationType::XX}, {"YY", CARTA::PolarizationType::YY},
    {"XY", CARTA::PolarizationType::XY}, {"YX", CARTA::PolarizationType::YX}, {"Ptotal", CARTA::PolarizationType::Ptotal},
    {"Plinear", CARTA::PolarizationType::Plinear}, {"PFtotal", CARTA::PolarizationType::PFtotal},
    {"PFlinear", CARTA::PolarizationType::PFlinear}, {"Pangle", CARTA::PolarizationType::Pangle}};

int GetStokesValue(const CARTA::PolarizationType& stokes_type);
CARTA::PolarizationType GetStokesType(int stokes_value);
bool ComputedStokes(int stokes);
bool ComputedStokes(const std::string& stokes_type);

// The struct StokesSrc is used to tell the file loader to get the original image interface, or get the computed stokes image interface.
// The x, y, and z ranges from the StokesSrc indicate the range of image data to be calculated (for the new stokes type image).
// We usually don't want to calculate the whole image data, because it spends a lot of time.
// StokesSrc will bind casacore::Slicer or casacore::ImageRegion, because the coordinate of a computed stokes image is different from
// the original image coordinate.
struct StokesSrc {
    int stokes;
    AxisRange z_range;
    AxisRange x_range;
    AxisRange y_range;

    StokesSrc() : stokes(-1), z_range(AxisRange(ALL_Z)), x_range(AxisRange(ALL_X)), y_range(AxisRange(ALL_Y)) {}

    StokesSrc(int stokes_, AxisRange z_range_) : stokes(stokes_), z_range(z_range_), x_range(ALL_X), y_range(ALL_Y) {}

    StokesSrc(int stokes_, AxisRange z_range_, AxisRange x_range_, AxisRange y_range_)
        : stokes(stokes_), z_range(z_range_), x_range(x_range_), y_range(y_range_) {}

    bool OriginalImage() const {
        return !ComputedStokes(stokes);
    }
    bool operator==(const StokesSrc& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return false;
        }
        return true;
    }
    bool operator!=(const StokesSrc& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return true;
        }
        return false;
    }
};

#endif // CARTA_BACKEND__UTIL_IMAGE_H_
