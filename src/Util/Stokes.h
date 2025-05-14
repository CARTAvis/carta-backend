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
    static Pol Get(int value);

    /**
     * @brief Retrieves the corresponding CARTA polarization type from a string name.
     *
     * @param name The string representation of a CARTA polarization type.
     * @return The corresponding CARTA polarization type if parsing is successful, otherwise `POLARIZATION_TYPE_NONE`.
     */
    static Pol Get(std::string name);

    /**
     * @brief Converts a CARTA polarization type to the corresponding CASA Stokes type.
     *
     * @param type The CARTA polarization type to convert.
     * @return The corresponding CASA Stokes type.
     * @throws std::out_of_range If the provided type is not found in the mapping.
     */
    static CasaPol ToCasa(Pol type);

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
    static std::string Name(Pol type);

    /**
     * @brief Retrieves a descriptive string for a given CARTA polarization type.
     *
     * @param[in] type The polarization type for which to retrieve a description.
     * @return A descriptive string for the given polarization type.
     */
    static std::string Description(Pol type);

    /**
     * @brief Determines if a given polarization type is a computed polarization.
     *
     * @param[in] value The integer representation of a CARTA polarization type.
     * @return `true` if the value corresponds to a computed polarization type, otherwise `false`.
     */
    static bool IsComputed(int value);

    /** @brief Retrieves the component polarizations required to calculate the given computed polarization.
     * 
     * @param[in] type The computed polarization type.
     * @return A vector of the required component polarizations.
     */
    static std::vector<Pol> Components(Pol type);

protected:
    /**
     * @brief Maps computed polarization types to the component polarizations required to calculate them.
     */
    static std::unordered_map<Pol, std::vector<Pol>> _components;
    
    /**
     * @brief Maps CARTA polarization types to CASA Stokes types.
     */
    static std::unordered_map<Pol, CasaPol> _to_casa;

    /**
     * @brief Provides human-readable descriptions for CARTA polarization types.
     */
    static std::unordered_map<Pol, std::string> _description;
};

// The struct StokesSource is used to tell the file loader to get the original image interface, or get the computed stokes image interface.
// The x, y, and z ranges from the StokesSource indicate the range of image data to be calculated (for the new stokes type image).
// We usually don't want to calculate the whole image data, because it spends a lot of time.
// StokesSource will bind casacore::Slicer or casacore::ImageRegion, because the coordinate of a computed stokes image is different from
// the original image coordinate.

/**
 * @brief Represents a source of Stokes data with axis range specifications.
 *
 * This struct encapsulates information about a Stokes parameter and its associated
 * axis ranges in an image dataset. It is used to tell the file loader to get the original
 * image interface, or get the computed stokes image interface.
 * It provides constructors for different levels of
 * specification and utility functions for comparison and checking if the data
 * represents an original image.
 */
struct StokesSource {
    int stokes;        ///< The Stokes parameter identifier.
    AxisRange z_range; ///< The range along the Z-axis.
    AxisRange x_range; ///< The range along the X-axis.
    AxisRange y_range; ///< The range along the Y-axis.

    /**
     * @brief Default constructor initializes with undefined Stokes and full axis ranges.
     */
    StokesSource() : stokes(-1), z_range(AxisRange(ALL_Z)), x_range(AxisRange(ALL_X)), y_range(AxisRange(ALL_Y)) {}

    /**
     * @brief Constructor initializing with a Stokes parameter and Z-axis range.
     *
     * @param[in] stokes_ The Stokes parameter.
     * @param[in] z_range_ The Z-axis range.
     */
    StokesSource(int stokes_, AxisRange z_range_) : stokes(stokes_), z_range(z_range_), x_range(ALL_X), y_range(ALL_Y) {}

    /**
     * @brief Constructor initializing with a Stokes parameter and specified axis ranges.
     *
     * @param[in] stokes_ The Stokes parameter.
     * @param[in] z_range_ The Z-axis range.
     * @param[in] x_range_ The X-axis range.
     * @param[in] y_range_ The Y-axis range.
     */
    StokesSource(int stokes_, AxisRange z_range_, AxisRange x_range_, AxisRange y_range_)
        : stokes(stokes_), z_range(z_range_), x_range(x_range_), y_range(y_range_) {}

    /**
     * @brief Checks if this Stokes source represents an original image.
     *
     * @return `true` if the Stokes parameter is not a computed polarization, otherwise `false`.
     */
    bool IsOriginalImage() const {
        return !Stokes::IsComputed(stokes);
    }

    /**
     * @brief Equality operator to compare two StokesSource objects.
     *
     * @param[in] rhs The StokesSource object to compare against.
     * @return `true` if all attributes match, otherwise `false`.
     */
    bool operator==(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return false;
        }
        return true;
    }

    /**
     * @brief Inequality operator to compare two StokesSource objects.
     *
     * @param[in] rhs The StokesSource object to compare against.
     * @return `true` if any attribute differs, otherwise `false`.
     */
    bool operator!=(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return true;
        }
        return false;
    }
};

} // namespace carta

#endif // CARTA_SRC_UTIL_STOKES_H_
