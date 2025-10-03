/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Stokes.h"
#include <algorithm>

using namespace carta;

/** @details This map stores the component polarizations required to calculate each computed polarization type. It is used to determine
 * whether a polarization can be computed from the polarizations available in an image file. The map is ordered so that Stokes::Computable
 * does not have to sort its output.
 */
std::map<Stokes::Pol, std::vector<Stokes::Pol>> Stokes::_components{{Pol::Ptotal, {Pol::Q, Pol::U, Pol::V}},
    {Pol::Plinear, {Pol::Q, Pol::U}}, {Pol::PFtotal, {Pol::I, Pol::Q, Pol::U, Pol::V}}, {Pol::PFlinear, {Pol::I, Pol::Q, Pol::U}},
    {Pol::Pangle, {Pol::Q, Pol::U}}};

/**
 * @details This unordered map provides a conversion between the CARTA polarization type
 * enumeration and the corresponding CASA Stokes type enumeration.
 * It is used to translate polarization representations between the two frameworks.
 */
std::unordered_map<Stokes::Pol, Stokes::CasaPol> Stokes::_to_casa{{Pol::POLARIZATION_TYPE_NONE, CasaPol::Undefined}, {Pol::I, CasaPol::I},
    {Pol::Q, CasaPol::Q}, {Pol::U, CasaPol::U}, {Pol::V, CasaPol::V}, {Pol::RR, CasaPol::RR}, {Pol::LL, CasaPol::LL},
    {Pol::RL, CasaPol::RL}, {Pol::LR, CasaPol::LR}, {Pol::XX, CasaPol::XX}, {Pol::YY, CasaPol::YY}, {Pol::XY, CasaPol::XY},
    {Pol::YX, CasaPol::YX}, {Pol::Ptotal, CasaPol::Ptotal}, {Pol::Plinear, CasaPol::Plinear}, {Pol::PFtotal, CasaPol::PFtotal},
    {Pol::PFlinear, CasaPol::PFlinear}, {Pol::Pangle, CasaPol::Pangle}};

/**
 * @details This unordered map associates each CARTA polarization type enumeration value
 * with a corresponding descriptive string. It is used to provide user-friendly
 * labels for polarization types in logs, UI displays, or reports.
 */
std::unordered_map<Stokes::Pol, std::string> Stokes::_description{{Pol::POLARIZATION_TYPE_NONE, "Unknown"}, {Pol::I, "Stokes I"},
    {Pol::Q, "Stokes Q"}, {Pol::U, "Stokes U"}, {Pol::V, "Stokes V"}, {Pol::Ptotal, "Total polarization intensity"},
    {Pol::Plinear, "Linear polarization intensity"}, {Pol::PFtotal, "Fractional total polarization intensity"},
    {Pol::PFlinear, "Fractional linear polarization intensity"}, {Pol::Pangle, "Polarization angle"}};

/**
 * @details This function checks if the provided integer value is a valid CARTA polarization type.
 * If valid, it returns the corresponding enumeration value. Otherwise, it returns
 * `CARTA::PolarizationType::POLARIZATION_TYPE_NONE` as a fallback.
 */
Stokes::Pol Stokes::Get(const int value) {
    if (CARTA::PolarizationType_IsValid(value)) {
        return static_cast<Pol>(value);
    }
    return Pol::POLARIZATION_TYPE_NONE;
}

/**
 * @details This function attempts to parse a given string into a CARTA polarization type.
 * If parsing is successful, it returns the corresponding enumeration value.
 * If the name is invalid, it returns `CARTA::PolarizationType::POLARIZATION_TYPE_NONE`.
 */
Stokes::Pol Stokes::Get(const std::string name) {
    auto type = Pol::POLARIZATION_TYPE_NONE;
    CARTA::PolarizationType_Parse(name, &type);
    return type;
}

/**
 * @details This function maps a CARTA polarization type to its equivalent CASA Stokes type
 * using a predefined lookup table. If the provided type is not found in the mapping,
 * an `std::out_of_range` exception may be thrown.
 */
Stokes::CasaPol Stokes::ToCasa(const Pol type) {
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
 * @details This function returns the string representation of a CARTA polarization type
 * using the `CARTA::PolarizationType_Name` function.
 */
std::string Stokes::Name(const Pol type) {
    return CARTA::PolarizationType_Name(type);
}

/**
 * @details This function returns a human-readable description of a CARTA polarization type
 * from the `_description` map. If the type is not found in the map, it falls back
 * to returning the string representation of the polarization type.
 */
std::string Stokes::Description(const Pol type) {
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
bool Stokes::IsComputed(const int value) {
    return (value >= Pol::Ptotal) && (value <= Pol::Pangle);
}

/** @details This function returns a vector containing the polarization types required to calculate the given computed polarization type,
 * using the `_components` map. For example, for `Ptotal` it returns `Q`, `U`, and `V`. If the input value is not found in the map, an empty
 * vector is returned.
 */
std::vector<Stokes::Pol> Stokes::Components(const Pol type) {
    try {
        return _components.at(type);
    } catch (const std::out_of_range& e) {
        return std::vector<Pol>();
    }
}

/** @details This function retrieves the polarizations which may be computed from the given component polarizations, using the `_components`
 * map. The components may be given in any order. The returned polarizations are sorted by numeric value. If no polarizations are computable
 * from the components provided, an empty vector is returned.
 */
std::vector<Stokes::Pol> Stokes::Computable(const std::vector<Pol>& components) {
    // Ensure that components are sorted and deduplicated
    std::vector<Pol> available(components);
    std::sort(available.begin(), available.end());
    auto last = std::unique(available.begin(), available.end());
    available.erase(last, available.end());

    std::vector<Pol> computable;

    for (auto& [computed, required] : _components) {
        if (std::includes(required.begin(), required.end(), available.begin(), available.end())) {
            computable.push_back(computed);
        }
    }

    return computable;
}
