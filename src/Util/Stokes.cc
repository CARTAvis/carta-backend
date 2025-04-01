/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Stokes.h"

using namespace carta;

/**
 * @details This unordered map provides a conversion between the `CARTA::PolarizationType`
 * enumeration and the corresponding `casacore::Stokes::StokesTypes` enumeration.
 * It is used to translate polarization representations between the two frameworks.
 */
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

/**
 * @details This unordered map associates each `CARTA::PolarizationType` enumeration value
 * with a corresponding descriptive string. It is used to provide user-friendly
 * labels for polarization types in logs, UI displays, or reports.
 */
std::unordered_map<CARTA::PolarizationType, std::string> Stokes::_description{{CARTA::PolarizationType::POLARIZATION_TYPE_NONE, "Unknown"},
    {CARTA::PolarizationType::I, "Stokes I"}, {CARTA::PolarizationType::Q, "Stokes Q"}, {CARTA::PolarizationType::U, "Stokes U"},
    {CARTA::PolarizationType::V, "Stokes V"}, {CARTA::PolarizationType::Ptotal, "Total polarization intensity"},
    {CARTA::PolarizationType::Plinear, "Linear polarization intensity"},
    {CARTA::PolarizationType::PFtotal, "Fractional total polarization intensity"},
    {CARTA::PolarizationType::PFlinear, "Fractional linear polarization intensity"},
    {CARTA::PolarizationType::Pangle, "Polarization angle"}};


/**
 * @details This function checks if the provided integer value is a valid `CARTA::PolarizationType`.
 * If valid, it returns the corresponding enumeration value. Otherwise, it returns
 * `CARTA::PolarizationType::POLARIZATION_TYPE_NONE` as a fallback.
 */
CARTA::PolarizationType Stokes::Get(int value) {
    if (CARTA::PolarizationType_IsValid(value)) {
        return static_cast<CARTA::PolarizationType>(value);
    }
    return CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
}

/**
 * @details This function attempts to parse a given string into a `CARTA::PolarizationType`.
 * If parsing is successful, it returns the corresponding enumeration value.
 * If the name is invalid, it returns `CARTA::PolarizationType::POLARIZATION_TYPE_NONE`.
 */
CARTA::PolarizationType Stokes::Get(std::string name) {
    auto type = CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
    CARTA::PolarizationType_Parse(name, &type);
    return type;
}

/**
 * @details This function maps a `CARTA::PolarizationType` to its equivalent `casacore::Stokes::StokesTypes`
 * using a predefined lookup table. If the provided type is not found in the mapping,
 * an `std::out_of_range` exception may be thrown.
 */
casacore::Stokes::StokesTypes Stokes::ToCasa(CARTA::PolarizationType type) {
    return _to_casa.at(type);
}

/**
 * @details This function maps a FITS Stokes parameter to a valid internal Stokes value.
 * It supports conversion of standard Stokes parameters (1 to 4) and
 * circular/linear polarization parameters (5 to 12 and -1 to -8).
 * 
 * @note Valid FITS Stokes values:
 *       - `1` to `4` (directly assigned)
 *       - `5` to `12` and `-1` to `-8` (converted using `out_stokes_value = -in_stokes_value + 4`)
 */
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

/**
 * @details This function returns the string representation of a `CARTA::PolarizationType`
 * using the `CARTA::PolarizationType_Name` function.
 */
std::string Stokes::Name(CARTA::PolarizationType type) {
    return CARTA::PolarizationType_Name(type);
}

/**
 * @details This function returns a human-readable description of a `CARTA::PolarizationType`
 * from the `_description` map. If the type is not found in the map, it falls back
 * to returning the string representation of the polarization type.
 */
std::string Stokes::Description(CARTA::PolarizationType type) {
    try {
        return _description.at(type);
    } catch (const std::out_of_range& e) {
        return CARTA::PolarizationType_Name(type);
    }
}

/**
 * @details This function checks whether the provided integer value corresponds to
 * a computed polarization type (e.g., `Ptotal`, `Plinear`, `PFtotal`, `PFlinear`, `Pangle`).
 */
bool Stokes::IsComputed(int value) {
    return (value >= CARTA::PolarizationType::Ptotal) && (value <= CARTA::PolarizationType::Pangle);
}
