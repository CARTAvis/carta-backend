/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Stokes.h"

using namespace carta;

std::unordered_map<CARTA::PolarizationType, casacore::Stokes::StokesTypes> Stokes::_to_casa{
    {CARTA::PolarizationType::POLARIZATION_TYPE_NONE, casacore::Stokes::StokesTypes::Undefined},
    {CARTA::PolarizationType::I, casacore::Stokes::StokesTypes::I}, {CARTA::PolarizationType::Q, casacore::Stokes::StokesTypes::Q},
    {CARTA::PolarizationType::U, casacore::Stokes::StokesTypes::U}, {CARTA::PolarizationType::V, casacore::Stokes::StokesTypes::V},
    {CARTA::PolarizationType::RR, casacore::Stokes::StokesTypes::RR}, {CARTA::PolarizationType::LL, casacore::Stokes::StokesTypes::LL},
    {CARTA::PolarizationType::RL, casacore::Stokes::StokesTypes::RL}, {CARTA::PolarizationType::LR, casacore::Stokes::StokesTypes::LR},
    {CARTA::PolarizationType::XX, casacore::Stokes::StokesTypes::XX}, {CARTA::PolarizationType::YY, casacore::Stokes::StokesTypes::YY},
    {CARTA::PolarizationType::XY, casacore::Stokes::StokesTypes::XY}, {CARTA::PolarizationType::YX, casacore::Stokes::StokesTypes::YX},
    {CARTA::PolarizationType::Ptotal, casacore::Stokes::StokesTypes::Ptotal},
    {CARTA::PolarizationType::Plinear, casacore::Stokes::StokesTypes::Plinear},
    {CARTA::PolarizationType::PFtotal, casacore::Stokes::StokesTypes::PFtotal},
    {CARTA::PolarizationType::PFlinear, casacore::Stokes::StokesTypes::PFlinear},
    {CARTA::PolarizationType::Pangle, casacore::Stokes::StokesTypes::Pangle}};

std::unordered_map<CARTA::PolarizationType, std::string> Stokes::_description{{CARTA::PolarizationType::POLARIZATION_TYPE_NONE, "Unknown"},
    {CARTA::PolarizationType::I, "Stokes I"}, {CARTA::PolarizationType::Q, "Stokes Q"}, {CARTA::PolarizationType::U, "Stokes U"},
    {CARTA::PolarizationType::V, "Stokes V"}, {CARTA::PolarizationType::Ptotal, "Total polarization intensity"},
    {CARTA::PolarizationType::Plinear, "Linear polarization intensity"},
    {CARTA::PolarizationType::PFtotal, "Fractional total polarization intensity"},
    {CARTA::PolarizationType::PFlinear, "Fractional linear polarization intensity"},
    {CARTA::PolarizationType::Pangle, "Polarization angle"}};

CARTA::PolarizationType Stokes::Get(int value) {
    if (CARTA::PolarizationType_IsValid(value)) {
        return static_cast<CARTA::PolarizationType>(value);
    }
    return CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
}

CARTA::PolarizationType Stokes::Get(std::string name) {
    auto type = CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
    CARTA::PolarizationType_Parse(name, &type);
    return type;
}

casacore::Stokes::StokesTypes Stokes::ToCasa(CARTA::PolarizationType type) {
    return _to_casa.at(type);
}

bool Stokes::ConvertFits(const int& in_stokes_value, int& out_stokes_value) {
    if (in_stokes_value >= 1 && in_stokes_value <= 4) {
        out_stokes_value = in_stokes_value;
        return true;
    } else if ((in_stokes_value >= 4 && in_stokes_value <= 12) || (in_stokes_value <= -1 && in_stokes_value >= -8)) {
        // convert between [5, 6, ..., 12] and [-1, -2, ..., -8]
        out_stokes_value = -in_stokes_value + 4;
        return true;
    }
    return false;
}

std::string Stokes::Name(CARTA::PolarizationType type) {
    return CARTA::PolarizationType_Name(type);
}

std::string Stokes::Description(CARTA::PolarizationType type) {
    try {
        return _description.at(type);
    } catch (const std::out_of_range& e) {
        return CARTA::PolarizationType_Name(type);
    }
}

bool Stokes::IsComputed(int value) {
    return (value >= CARTA::PolarizationType::Ptotal) && (value <= CARTA::PolarizationType::Pangle);
}
