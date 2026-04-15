/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_DATASTREAM_VECTORFIELD_H_
#define CARTA_SRC_DATASTREAM_VECTORFIELD_H_

#include <list>
#include <memory>
#include <shared_mutex>

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <casacore/casa/BasicSL/Constants.h>

#include "DataStream/Compression.h"
#include "DataStream/Tile.h"
#include "Util/Image.h"
#include "Util/Nan.h"

namespace carta {

/// @brief Mostly for cosmetics and make the code more compact instead of using the full protobuf name of the enum
using Pol = CARTA::PolarizationType;

/// @brief Type of the callback function passed to VectorFieldCalculator::Calculate in order to fill the data array
///        returned to the front-end in the protobuf message
using TileCallback = const std::function<bool(std::vector<float>&, CARTA::ImageBounds&, int, Pol, int&, int&)>; // was &

/**
 * @class VectorFieldCalculator
 * @brief Performs calculations required to overlay polarisation vectors on top of a sky image.
 *
 * This class provides functions to perform all the necessary calculations based on the parameters provided in the protobuf message
 *
 */
class VectorFieldCalculator {
public:
    /// @brief Enum defining all possible sources of data for Vector Field calculation and thresholding
    enum class Source { NONE, CURRENT, I, PA, PI, FPI };

    /**
     * @brief Constructs the object
     *
     * This constructor takes a protobuf message with parameters
     * to initialise all the parameters of the calculation
     *
     * @param message Parameters of the calculations as specified in the front-end Vector Overlay widget.
     */
    VectorFieldCalculator(const CARTA::SetVectorOverlayParameters& message);

    /**
     * @brief Invokes the calculation of the vector overlay
     *
     * This function performs the actual calculations of the overlaid vector field (polarisation)
     * and returns the tiles containing the generated image via callback function progress_callback
     * The calculation has been highly optimised so that only a single pass through the data is performed to
     * calculate the requested values (e.g. PI, FPI or PA) and quantities needed to apply the threshold.
     * There is a little bit of code duplication (certain loops), but they are required to make a single pass through the data.
     * They also make the code easier to understand and follow by avoid complex conditionals inside the loops,
     * which may make it a bit faster (most likely negligible though).

     *
     * @param progress_callback The callback function returning the results of the calculation to the caller
     * @param dims Specifies the dimensions of the images (width and height)
     * @param tile_callback The callback function providing input images and filling the arrays corresponding to tiles
     *
     * @return returns true if calculation is successful. The actual tiles with the resulting image data are send via callback tile_callback
     */
    bool Calculate(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims, TileCallback tile_callback);

    /**
     * @brief Verifies if the calculation is still valid (true) or was invalidated by a new request
     */
    bool IsValid() {
        return _is_valid;
    }

    /**
     * @brief Invalidates on-going calculation and results in stopping it
     */
    void Invalidate() {
        _is_valid = false;
    }

    /**
     * @brief Checks if Vector Field calculation is disabled (does not need to be performed)
     */
    bool Disabled() {
        return (_intensity_source == Source::NONE && _angle_source == Source::NONE);
    }

    /**
     * @brief Checks if Vector Field calculation uses CURRENT as a source for intensity, angle or threshold
     */
    bool UsesCurrent() {
        return (_intensity_source == Source::CURRENT || _angle_source == Source::CURRENT || _threshold_source == Source::CURRENT);
    }

protected:
    void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
        std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);

    /// @name Parameters of the requested Vector Field calculation copied from the protobuf message
    /// These parameters are passed from the Vector Field calculation widget in the front-end
    /// @{
    int _file_id;                             ///< ID of the file
    int _smoothing_factor;                    ///< smoothing factor as defined in
    bool _fractional;                         ///< calculate fractional polarised intensity
    double _threshold;                        ///< a value of threshold to be applied (NaN means no thresholding)
    bool _debiasing;                          ///< whether debiasing should be applied
    double _q_error;                          ///< value of the Q polarisation error
    double _u_error;                          ///< value of the U polarisation error
    int _stokes_intensity;                    ///< specifies the sources of Stokes intensity
    int _stokes_angle;                        ///< specifies the source of polarisation angle
    CARTA::CompressionType _compression_type; ///< type of compression algorithm
    float _compression_quality;               ///< compression quality
    Pol _threshold_option;                    ///< specifies polarisation to which threshold should be applied
    /// @}

    /// @brief These properities specify the sources data data
    ///
    /// Possible values are: NONE, CURRENT, I, PA, PI, FPI
    Source _intensity_source; ///< source of data for intensity
    Source _angle_source;     ///< source of data for angle
    Source _threshold_source; ///< source of data for the threshold

    bool _is_valid; ///< indicates if the calculation is valid and should be continued
};

/**
 * @class VectorField
 * @brief Manages VectorFieldCalculator objects
 *
 * This class the calculator objects VectorFieldCalculator and provides functions to set parameters (SetVectorOverlayParameters),
 * start new calculation (NewCalculation) and cancel on-going calculation (StopCalculations)
 *
 */
class VectorField {
public:
    VectorField();

    /**
     * @brief Sets the parameters of the calculation as set in the front-end VectorOverlay widget
     *
     * @param parameters Protobuf message with parameters of the calculation specified in the front-end
     */
    void SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& parameters);

    /**
     * @brief Starts new calculation of the vector field and invalidates all on-going calculations
     *
     * @param progress_callback The callback function returning the results of the calculation to the caller (passed to the
     * VectorFieldCalculator::Calculate function)
     * @param dims Specifies the dimensions of the images (width and height). Also passed to the
     * VectorFieldCalculator::Calculate function
     * @param tile_callback The callback function providing input images and filling the arrays corresponding to tiles (passed to the
     * VectorFieldCalculator::Calculate function)
     * @param stokes_changed The flag specifying if the Stokes image has changed
     * @param z_changed The flag specifying if the Z axis has changed
     */
    bool NewCalculation(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims,
        TileCallback tile_callback, bool stokes_changed = false, bool z_changed = false);

    /**
     * @brief Invalidates all the on-going calculations effectively stopping them.
     */
    void StopCalculations();

protected:
    /**
     * @brief Flag set by StopCalculations if true all the on-going calcutions have been stopped
     */
    bool _stopped;

    /// @brief Parameters of vector field calculation as passed from the front end in a protobuf message
    CARTA::SetVectorOverlayParameters _parameters;

    /// @name Objects for Vector Field Calculator
    /// Objects controlling the on-going Vector Field calculations
    /// @{
    std::mutex _mutex;                                                ///< Mutex preventing from changing _calculators in the same time
    std::vector<std::shared_ptr<VectorFieldCalculator>> _calculators; ///< vector of Vector field calculator objects
                                                                      ///< to perform multiple calculations in parallel
    /// @}
};

} // namespace carta

#endif // CARTA_SRC_DATASTREAM_VECTORFIELD_H_
