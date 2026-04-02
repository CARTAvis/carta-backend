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

using Pol = CARTA::PolarizationType;
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
     * There is a little bit of code duplication below, but they are required to make a single pass through the data
     * They also make the code easier to understand and follow and this way we avoid more conditionals inside the loops, 
     * which may make it a bit faster (may be negligible though)

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

    void Invalidate() {
        _is_valid = false;
    }

    bool Disabled() {
        return (_intensity_source == Source::NONE && _angle_source == Source::NONE);
    }

    bool UsesCurrent() {
        return (_intensity_source == Source::CURRENT || _angle_source == Source::CURRENT || _threshold_source == Source::CURRENT);
    }
    
    bool SourceCurrent() {
       return (_intensity_source == Source::CURRENT || _angle_source == Source::CURRENT);
    }


protected:
    void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
        std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);

    int _file_id;
    int _smoothing_factor;
    bool _fractional;
    double _threshold;
    bool _debiasing;
    double _q_error;
    double _u_error;
    int _stokes_intensity;
    int _stokes_angle;
    CARTA::CompressionType _compression_type;
    float _compression_quality;
    Pol _threshold_option;

    // sources of data, possible values: NONE, CURRENT, I, PA, PI, FPI
    Source _intensity_source;
    Source _angle_source;
    Source _threshold_source;

    // indicates if the calculation is valid and should be continued :
    bool _is_valid;
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
     * @param stokes_changed The flag specifying if the Stokes image has changed (TODO : confirm what it is ?)
     * @param z_changed The flag specifying if the Z axis has changed (TODO : confirm what it is ?)
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

    // Vector field settings
    CARTA::SetVectorOverlayParameters _parameters;
    std::mutex _mutex;
    std::vector<std::shared_ptr<VectorFieldCalculator>>
        _calculators; // TBD/TODO : list or vector - depends if we need to delete elements in the middle (list may be better for this)
};

} // namespace carta

#endif // CARTA_SRC_DATASTREAM_VECTORFIELD_H_
