/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_STOKES_H_
#define CARTA_SRC_UTIL_STOKES_H_

#include <string>

#include <carta-protobuf/enums.pb.h>

#include <casacore/measures/Measures/Stokes.h>

#include "Image.h"

// stokes types and value conversion
static std::unordered_map<CARTA::PolarizationType, int> StokesValues{{CARTA::PolarizationType::I, 1}, {CARTA::PolarizationType::Q, 2},
    {CARTA::PolarizationType::U, 3}, {CARTA::PolarizationType::V, 4}, {CARTA::PolarizationType::RR, 5}, {CARTA::PolarizationType::LL, 6},
    {CARTA::PolarizationType::RL, 7}, {CARTA::PolarizationType::LR, 8}, {CARTA::PolarizationType::XX, 9}, {CARTA::PolarizationType::YY, 10},
    {CARTA::PolarizationType::XY, 11}, {CARTA::PolarizationType::YX, 12}, {CARTA::PolarizationType::Ptotal, 13},
    {CARTA::PolarizationType::Plinear, 14}, {CARTA::PolarizationType::PFtotal, 15}, {CARTA::PolarizationType::PFlinear, 16},
    {CARTA::PolarizationType::Pangle, 17}};

// computed stokes using StokesValues
#define COMPUTE_STOKES_PTOTAL StokesValues[CARTA::PolarizationType::Ptotal]     // Total polarization intensity: (Q^2+U^2+V^2)^(1/2)
#define COMPUTE_STOKES_PLINEAR StokesValues[CARTA::PolarizationType::Plinear]   // Linear polarization intensity: (Q^2+U^2)^(1/2)
#define COMPUTE_STOKES_PFTOTAL StokesValues[CARTA::PolarizationType::PFtotal]   // Fractional total polarization intensity: Ptotal/I
#define COMPUTE_STOKES_PFLINEAR StokesValues[CARTA::PolarizationType::PFlinear] // Fractional linear polarization intensity: Plinear/I
#define COMPUTE_STOKES_PANGLE StokesValues[CARTA::PolarizationType::Pangle]     // Polarization angle: (tan^-1(U/Q))/2

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

static std::unordered_map<int, casacore::Stokes::StokesTypes> StokesTypesToCasacore{{1, casacore::Stokes::StokesTypes::I},
    {2, casacore::Stokes::StokesTypes::Q}, {3, casacore::Stokes::StokesTypes::U}, {4, casacore::Stokes::StokesTypes::V},
    {5, casacore::Stokes::StokesTypes::RR}, {6, casacore::Stokes::StokesTypes::LL}, {7, casacore::Stokes::StokesTypes::RL},
    {8, casacore::Stokes::StokesTypes::LR}, {9, casacore::Stokes::StokesTypes::XX}, {10, casacore::Stokes::StokesTypes::YY},
    {11, casacore::Stokes::StokesTypes::XY}, {12, casacore::Stokes::StokesTypes::YX}, {13, casacore::Stokes::StokesTypes::Ptotal},
    {14, casacore::Stokes::StokesTypes::Plinear}, {15, casacore::Stokes::StokesTypes::PFtotal},
    {16, casacore::Stokes::StokesTypes::PFlinear}, {17, casacore::Stokes::StokesTypes::Pangle}};

int GetStokesValue(const CARTA::PolarizationType& stokes_type);
CARTA::PolarizationType GetStokesType(int stokes_value);
bool IsComputedStokes(int stokes);
bool IsComputedStokes(const std::string& stokes);

// The struct StokesSource is used to tell the file loader to get the original image interface, or get the computed stokes image interface.
// The x, y, and z ranges from the StokesSource indicate the range of image data to be calculated (for the new stokes type image).
// We usually don't want to calculate the whole image data, because it spends a lot of time.
// StokesSource will bind casacore::Slicer or casacore::ImageRegion, because the coordinate of a computed stokes image is different from
// the original image coordinate.
struct StokesSource {
    int stokes;
    AxisRange z_range;
    AxisRange x_range;
    AxisRange y_range;

    StokesSource() : stokes(-1), z_range(AxisRange(ALL_Z)), x_range(AxisRange(ALL_X)), y_range(AxisRange(ALL_Y)) {}

    StokesSource(int stokes_, AxisRange z_range_) : stokes(stokes_), z_range(z_range_), x_range(ALL_X), y_range(ALL_Y) {}

    StokesSource(int stokes_, AxisRange z_range_, AxisRange x_range_, AxisRange y_range_)
        : stokes(stokes_), z_range(z_range_), x_range(x_range_), y_range(y_range_) {}

    bool IsOriginalImage() const {
        return !IsComputedStokes(stokes);
    }
    bool operator==(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return false;
        }
        return true;
    }
    bool operator!=(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return true;
        }
        return false;
    }
};

// Get computed stokes value
float CalcPtotal(float val_q, float val_u, float val_v);
float CalcPlinear(float val_q, float val_u);
float CalcPFtotal(float val_i, float val_q, float val_u, float val_v);
float CalcPFlinear(float val_i, float val_q, float val_u);
float CalcPangle(float val_q, float val_u);

#endif // CARTA_SRC_UTIL_STOKES_H_
