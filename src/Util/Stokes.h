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

namespace carta {

class Stokes {
public:
    using Pol = CARTA::PolarizationType;
    using CasaPol = casacore::Stokes::StokesTypes;

    /**
     * @brief Retrieves the corresponding CARTA polarization type from an integer value.
     *
     * @param value The integer representation of a CARTA polarization type.
     * @return The corresponding CARTA polarization type if valid, otherwise `POLARIZATION_TYPE_NONE`.
     */
    static Pol Get(const int value);

    /**
     * @brief Retrieves the corresponding CARTA polarization type from a string name.
     *
     * @param name The string representation of a CARTA polarization type.
     * @return The corresponding CARTA polarization type if parsing is successful, otherwise `POLARIZATION_TYPE_NONE`.
     */
    static Pol Get(const std::string name);

    /**
     * @brief Converts a CARTA polarization type to the corresponding CASA Stokes type.
     *
     * @param type The CARTA polarization type to convert.
     * @return The corresponding CASA Stokes type.
     * @throws std::out_of_range If the provided type is not found in the mapping.
     */
    static CasaPol ToCasa(const Pol type);

    /**
     * @brief Converts a FITS Stokes parameter value to its corresponding internal representation.
     *
     * @param[in] in_stokes_value The input FITS Stokes parameter value.
     * @param[out] out_stokes_value The converted Stokes parameter value.
     * @return `true` if the conversion was successful, `false` if the input value is invalid.
     */
    static bool ConvertFits(const int& in_stokes_value, int& out_stokes_value);

    /**
     * @brief Retrieves the name of a given CARTA polarization type.
     *
     * @param[in] type The polarization type to retrieve the name for.
     * @return The string representation of the given polarization type.
     */
    static std::string Name(const Pol type);

    /**
     * @brief Retrieves a descriptive string for a given CARTA polarization type.
     *
     * @param[in] type The polarization type for which to retrieve a description.
     * @return A descriptive string for the given polarization type.
     */
    static std::string Description(const Pol type);

    /**
     * @brief Determines if a given polarization type is a computed polarization.
     *
     * @param[in] value The integer representation of a CARTA polarization type.
     * @return `true` if the value corresponds to a computed polarization type, otherwise `false`.
     */
    static bool IsComputed(const int value);

    /** @brief Retrieves the component polarizations required to calculate the given computed polarization.
     * 
     * @param[in] type The computed polarization type.
     * @return A vector of the required component polarizations.
     */
    static std::vector<Pol> Components(const Pol type);
    

    /** @brief Retrieves the polarizations which may be computed from the given component polarizations.
     * 
     * @param[in] components A vector of the available component polarizations.
     * @return A sorted vector of the computable polarizations.
     */
    static std::vector<Pol> Computable(const std::vector<Pol>& components);

protected:
    /**
     * @brief Maps computed polarization types to the component polarizations required to calculate them.
     */
    static std::map<Pol, std::vector<Pol>> _components;

    /**
     * @brief Maps CARTA polarization types to CASA Stokes types.
     */
    static std::unordered_map<Pol, CasaPol> _to_casa;

    /**
     * @brief Provides human-readable descriptions for CARTA polarization types.
     */
    static std::unordered_map<Pol, std::string> _description;
};

} // namespace carta

#endif // CARTA_SRC_UTIL_STOKES_H_
