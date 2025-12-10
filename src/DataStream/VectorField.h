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

using TileCallback = const std::function<bool(std::vector<float>&, CARTA::ImageBounds&, int, CARTA::PolarizationType, int&, int&)>; // was &

/**
 * @class VectorFieldCalculator
 * @brief Performs calculations required to overlay polarisation vectors on top of a sky image.
 *
 * This class provides functions to perform all the necessary calculations based on the parameters provided in the protobuf message
 *
 */
class VectorFieldCalculator {
public:
    enum class Source { NONE, CURRENT, I, PA, PI, FPI};
    
    /**
     * @brief Constructs the object
     *
     * This constructor takes a protobuf message with parameters and a flag has_stokes_axis
     * to initialise all the parameters of the calculation
     *
     * @param message Parameters of the calculations as specified in the front-end Vector Overlay widget.
     * @param has_stokes_axis The flag specifying if the image has the Stokes axis (images Q and U).
     */
    VectorFieldCalculator(const CARTA::SetVectorOverlayParameters& message, bool has_stokes_axis);

    /**
     * @brief Invokes the calculation of the vector overlay
     *
     * This function performs the actual calculations of the overlaid vector field (polarisation)
     * and returns the tiles containing the generated image via callback function progress_callback
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

    struct Valid {
        bool operator()(float a, float b) {
            return (!std::isnan(a) && !std::isnan(b));
        }
    };

    struct ThresholdCut {
        float threshold;
        Valid valid;

        ThresholdCut(float threshold_) : threshold(threshold_) {}

        float operator()(float data, float result) {
            return (std::isnan(data) || (!std::isnan(threshold) && (data < threshold))) ? FLOAT_NAN : result;
        }

        void operator()(float& data) {
            if (valid(threshold, data) && data < threshold) {
                data = FLOAT_NAN;
            }
        }
    };

    struct CalcPi {
        double q_error;
        double u_error;
        Valid valid;

        CalcPi(double q_error_, double u_error_) : q_error(q_error_), u_error(u_error_) {}

        float operator()(float q, float u) {
            if (valid(q, u)) {
                return ((float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - (std::pow(q_error, 2) + std::pow(u_error, 2)) / 2.0));
            }
            return FLOAT_NAN;
        }
    };

    struct CalcFpi {
        Valid valid;

        float operator()(float i, float pi) {
            return (valid(i, pi) ? (float)100.0 * (pi / i) : FLOAT_NAN);
        }
    };

    struct CalcPa {
        Valid valid;

        float operator()(float q, float u) {
            return (valid(q, u) ? ((float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2) : FLOAT_NAN);
        }
    };

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
    CARTA::PolarizationType _threshold_option;

    // Extra variables to be determined based on the existence of stokes axis
    bool _calculate_pi;
    bool _calculate_pa;
    bool _current_stokes_as_pi;
    bool _current_stokes_as_pa;

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
     * @param has_stokes_axis A flag specifying if the image has the Stokes axis (i.e. has Q and U images of the sky)
     * @param tile_callback The callback function providing input images and filling the arrays corresponding to tiles (passed to the
     * VectorFieldCalculator::Calculate function)
     * @param stokes_changed The flag specifying if the Stokes image has changed (TODO : confirm what it is ?)
     * @param z_changed The flag specifying if the Z axis has changed (TODO : confirm what it is ?)
     */
    bool NewCalculation(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims, bool has_stokes_axis,
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
