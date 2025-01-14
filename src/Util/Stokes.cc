/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Stokes.h"

using namespace carta;

std::unordered_map<Pol, std::vector<Pol>> Stokes::_components{
    {Pol::Ptotal, {Pol::Q, Pol::U, Pol::V}},
    {Pol::Plinear, {Pol::Q, Pol::U}},
    {Pol::PFtotal, {Pol::I, Pol::Q, Pol::U, Pol::V}},
    {Pol::PFlinear, {Pol::I, Pol::Q, Pol::U}},
    {Pol::Pangle, {Pol::Q, Pol::U}}};

std::unordered_map<Pol, CasaPol> Stokes::_to_casa{
    {Pol::POLARIZATION_TYPE_NONE, CasaPol::Undefined},
    {Pol::I, CasaPol::I}, {Pol::Q, CasaPol::Q},
    {Pol::U, CasaPol::U}, {Pol::V, CasaPol::V},
    {Pol::RR, CasaPol::RR}, {Pol::LL, CasaPol::LL},
    {Pol::RL, CasaPol::RL}, {Pol::LR, CasaPol::LR},
    {Pol::XX, CasaPol::XX}, {Pol::YY, CasaPol::YY},
    {Pol::XY, CasaPol::XY}, {Pol::YX, CasaPol::YX},
    {Pol::Ptotal, CasaPol::Ptotal},
    {Pol::Plinear, CasaPol::Plinear},
    {Pol::PFtotal, CasaPol::PFtotal},
    {Pol::PFlinear, CasaPol::PFlinear},
    {Pol::Pangle, CasaPol::Pangle}};

std::unordered_map<Pol, std::string> Stokes::_description{{Pol::POLARIZATION_TYPE_NONE, "Unknown"},
    {Pol::I, "Stokes I"}, {Pol::Q, "Stokes Q"}, {Pol::U, "Stokes U"},
    {Pol::V, "Stokes V"}, {Pol::Ptotal, "Total polarization intensity"},
    {Pol::Plinear, "Linear polarization intensity"},
    {Pol::PFtotal, "Fractional total polarization intensity"},
    {Pol::PFlinear, "Fractional linear polarization intensity"},
    {Pol::Pangle, "Polarization angle"}};

Pol Stokes::Get(int value) {
    if (Pol_IsValid(value)) {
        return static_cast<Pol>(value);
    }
    return Pol::POLARIZATION_TYPE_NONE;
}

Pol Stokes::Get(std::string name) {
    auto type = Pol::POLARIZATION_TYPE_NONE;
    Pol_Parse(name, &type);
    return type;
}

CasaPol Stokes::ToCasa(Pol type) {
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

std::string Stokes::Name(Pol type) {
    return CARTA::PolarizationType_Name(type);
}

std::string Stokes::Description(Pol type) {
    try {
        return _description.at(type);
    } catch (const std::out_of_range& e) {
        return CARTA::PolarizationType_Name(type);
    }
}

bool Stokes::IsComputed(int value) {
    return (value >= Pol::Ptotal) && (value <= Pol::Pangle);
}

std::vector<Pol> Stokes::Components(Pol type) {
    try {
        return _components.at(type);
    } catch (const std::out_of_range& e) {
        return std::vector<Pol>();
    }
}
