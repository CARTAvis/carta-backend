/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_IMAGE_H_
#define CARTA_SRC_UTIL_IMAGE_H_

#include <cmath>
#include <functional>
#include <string>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay_tile.pb.h>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Exceptions/Error.h>

#include "DataStream/Tile.h"

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0
#define NEW_REGION_ID -1 // SetRegion only
#define ALL_REGIONS -10
#define TEMP_REGION_ID -100
#define TEMP_FOV_REGION_ID -1000

// x axis
#define ALL_X -2

// y axis
#define ALL_Y -2

// z axis
#define DEFAULT_Z 0
#define CURRENT_Z -1
#define ALL_Z -2

// stokes (computed stokes in Stokes.h)
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

/**
 * @brief Represents a range along an axis.
 *
 * This struct stores a range with `from` and `to` values, providing
 * constructors for different initialization scenarios and utility
 * functions for comparison and range checking.
 */
struct AxisRange {
    int from; ///< The start of the range.
    int to;   ///< The end of the range.

    /**
     * @brief Default constructor initialising the range from 0 to ALL_Z.
     */
    AxisRange() {
        from = 0;
        to = ALL_Z;
    }

    /**
     * @brief Constructs a range with the same start and end value.
     *
     * @param from_and_to_ The value for both `from` and `to`.
     */
    AxisRange(int from_and_to_) {
        from = to = from_and_to_;
    }

    /**
     * @brief Constructs a range with a specified start and end.
     *
     * @param from_ The start of the range.
     * @param to_ The end of the range.
     */
    AxisRange(int from_, int to_) {
        from = from_;
        to = to_;
    }

    /**
     * @brief Checks if two AxisRange objects are equal.
     *
     * Two AxisRange objects are considered equal if both their `from`
     * and `to` values match.
     *
     * @param rhs The other AxisRange to compare with.
     * @return true if the two ranges are equal, false otherwise.
     */
    bool operator==(const AxisRange& rhs) const {
        if ((from == rhs.from) && (to == rhs.to)) {
            return true;
        }
        return false;
    }

    /**
     * @brief Checks if two AxisRange objects are not equal.
     *
     * Two AxisRange objects are considered not equal if either their
     * `from` or `to` values differ.
     *
     * @param rhs The other AxisRange to compare with.
     * @return true if the two ranges are not equal, false otherwise.
     */
    bool operator!=(const AxisRange& rhs) const {
        if ((from != rhs.from) || (to != rhs.to)) {
            return true;
        }
        return false;
    }

    /**
     * @brief Determines if a value falls within the range.
     *
     * @param val The value to check.
     * @return true if `val` is within the range [from, to], false otherwise.
     */
    bool is_in_range(int val) const {
        return val >= from && val <= to;
    }
};

/**
 * @struct PointXy
 * @brief Represents a 2D point with floating-point coordinates.
 *
 * This struct provides basic operations for handling 2D points,
 * such as assignment, comparison, and checking if a point is
 * within image bounds.
 */
struct PointXy {
    float x; ///< X-coordinate of the point.
    float y; ///< Y-coordinate of the point.

    /**
     * @brief Default constructor initialising point to (-1.0, -1.0).
     */
    PointXy() {
        x = -1.0;
        y = -1.0;
    }

    /**
     * @brief Constructor initialising point with given coordinates.
     * @param x_ The x-coordinate.
     * @param y_ The y-coordinate.
     */

    PointXy(float x_, float y_) {
        x = x_;
        y = y_;
    }

    /**
     * @brief Assignment operator.
     * @param other The point to assign from.
     */
    void operator=(const PointXy& other) {
        x = other.x;
        y = other.y;
    }

    /**
     * @brief Equality comparison operator.
     * @param rhs The point to compare with.
     * @return True if the points have the same coordinates, false otherwise.
     */
    bool operator==(const PointXy& rhs) const {
        if ((x != rhs.x) || (y != rhs.y)) {
            return false;
        }
        return true;
    }

    /**
     * @brief Converts floating-point coordinates to integer indices.
     * @param x_index Reference to store the x-coordinate index.
     * @param y_index Reference to store the y-coordinate index.
     */

    void ToIndex(int& x_index, int& y_index) {
        // convert float to int for index into image data array
        x_index = static_cast<int>(std::round(x));
        y_index = static_cast<int>(std::round(y));
    }

    /**
     * @brief Checks if the point is within the given image axis ranges.
     * @param xrange The width of the image.
     * @param yrange The height of the image.
     * @return True if the point is within the image bounds, false otherwise.
     */
    bool InImage(int xrange, int yrange) {
        // returns whether x, y are within given image axis ranges
        int x_index, y_index;
        ToIndex(x_index, y_index);
        bool x_in_image = (x_index >= 0) && (x_index < xrange);
        bool y_in_image = (y_index >= 0) && (y_index < yrange);
        return (x_in_image && y_in_image);
    }
};

/**
 * @struct AxesInfo
 * @brief Stores information about image axes, including spatial and spectral components.
 *
 * This struct holds indices for different axes used in image rendering, spatial mapping,
 * spectral representation, and stokes parameters.
 */
struct AxesInfo {
    int x;         ///< Index of the X-axis for rendering.
    int y;         ///< Index of the Y-axis for rendering.
    int spatial_x; ///< Index of the spatial X-axis.
    int spatial_y; ///< Index of the spatial Y-axis (if applicable).
    int spectral;  ///< Index of the spectral axis.
    int z;         ///< Index of the Z-axis (if applicable).
    int stokes;    ///< Index of the stokes axis (if applicable).

    /**
     * @brief Default constructor initialising all axes to -1 (undefined).
     */
    AxesInfo() : AxesInfo({-1, -1}, {-1, -1}, -1, -1, -1) {}

    /**
     * @brief Constructor initialising rendering and spatial axes.
     * @param render A vector containing the X and Y rendering axes.
     * @param spatial A vector containing the spatial X and Y axes.
     * @param spectral The spectral axis index.
     */
    AxesInfo(const std::vector<int> render, const std::vector<int> spatial, int spectral) : AxesInfo(render, spatial, spectral, -1, -1) {}

    /**
     * @brief Constructor initialising rendering, spatial, spectral, Z, and stokes axes.
     * @param render A vector containing the X and Y rendering axes.
     * @param spatial A vector containing the spatial X and Y axes.
     * @param spectral The spectral axis index.
     * @param z The Z-axis index.
     * @param stokes The stokes axis index.
     */
    AxesInfo(const std::vector<int> render, std::vector<int> spatial, int spectral, int z, int stokes)
        : x(render.at(0)), y(render.at(1)), spatial_x(spatial.at(0)), spatial_y(spatial.at(1)), spectral(spectral), z(z), stokes(stokes) {}

    /**
     * @brief Retrieves the rendering axes (X and Y).
     * @return A vector containing the X and Y rendering axes.
     */
    std::vector<int> Render() {
        return {x, y};
    }

    /**
     * @brief Retrieves the spatial axes (spatial_x and spatial_y).
     * @return A vector containing the spatial X and Y axes.
     */
    std::vector<int> Spatial() {
        return {spatial_x, spatial_y};
    }
};

/**
 * @struct DimsInfo
 * @brief Represents the dimensions of an image or data cube.
 *
 * This struct stores information about the width, height, depth, number of spectral channels,
 * and number of Stokes parameters based on the given axes and shape.
 */
struct DimsInfo {
    size_t width;        ///< Width of the data (corresponding to the X-axis).
    size_t height;       ///< Height of the data (corresponding to the Y-axis).
    size_t depth;        ///< Depth of the data (corresponding to the Z-axis).
    size_t num_channels; ///< Number of spectral channels.
    size_t num_stokes;   ///< Number of Stokes parameters.

    /**
     * @brief Retrieves the size of a given axis from the shape.
     * @param axis The axis index.
     * @param shape The shape of the data.
     * @return The size of the axis if valid, otherwise returns 1.
     */
    static size_t FromAxis(int axis, const casacore::IPosition& shape) {
        if (axis < 0)
            return 1;
        if (axis >= shape.nelements()) {
            throw casacore::AipsError("Axis index out of bounds in DimsInfo::FromAxis");
        }
        return shape(axis);
    }

    /**
     * @brief Default constructor initialising all dimensions to 1.
     */
    DimsInfo() : width(1), height(1), depth(1), num_channels(1), num_stokes(1) {}

    /**
     * @brief Constructor that initialises dimensions based on given axes and shape.
     * @param axes The AxesInfo struct containing axis indices.
     * @param shape The shape of the data in terms of dimensions.
     */
    DimsInfo(const AxesInfo& axes, const casacore::IPosition& shape)
        : width(FromAxis(axes.x, shape)),
          height(FromAxis(axes.y, shape)),
          depth(FromAxis(axes.z, shape)),
          num_channels(FromAxis(axes.spectral, shape)),
          num_stokes(FromAxis(axes.stokes, shape)) {}
};

#endif // CARTA_SRC_UTIL_IMAGE_H_
