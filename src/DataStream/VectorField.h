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

    struct ThresholdCut {
        const double _threshold;

        ThresholdCut(float threshold) : _threshold(threshold) {}

        float operator()(float data, float result) {
            return (std::isnan(data) || (!std::isnan(_threshold) && (data < _threshold))) ? FLOAT_NAN : result;
        }

        void operator()(float& d) {
            if (!std::isnan(_threshold) && !std::isnan(d) && d < _threshold) {
                d = FLOAT_NAN;
            }
        }
    };

    struct CalcPi {
        const double _error_term;
        const double _threshold;

        CalcPi(double q_error, double u_error, double threshold) : 
            _error_term((std::pow(q_error, 2) + std::pow(u_error, 2))/2.0), _threshold(threshold) {}

        float operator()(float q, float u) {
            if (!std::isnan(q) && !std::isnan(u)) {
                float pi = (float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term);
                if( std::isnan(pi) || (!std::isnan(_threshold) && (pi < _threshold)) ) {
                   return FLOAT_NAN;
                }

                return pi;
            }
            return FLOAT_NAN;
        }

        float operator()(float q, float u, float v, bool threshold_v) {
            if ( threshold_v ) {
               // v is a value to check against the threshold, otherwise it is a return value when threshold is applied to pi (in else):
               if (!std::isnan(v) && !std::isnan(_threshold) && (v >= _threshold) && !std::isnan(q) && !std::isnan(u)) {
                  return (float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term);
               }
            } else {
               // v is a return value when Pi >= threshold (in this case Pi is calculated as a threshold to display another value):
               if (!std::isnan(q) && !std::isnan(u)) {
                   float pi = (float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term);
                   if( std::isnan(pi) || (!std::isnan(_threshold) && (pi < _threshold)) ) {
                      return FLOAT_NAN;
                   }

                   return v;
               }
            }   
             
            return FLOAT_NAN;
        }

/*        float operator()(float q, float u, float t) {
            if (!std::isnan(t) && !std::isnan(_threshold) && (t >= _threshold) && !std::isnan(q) && !std::isnan(u)) {
                return (float)std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term);
            }
            return FLOAT_NAN;
        }*/
    };

    // TODO : do the same as above !
    struct CalcFpi {
        const double _error_term;
        const double _threshold;

        CalcFpi(double q_error, double u_error, double threshold) : _error_term((std::pow(q_error, 2) + std::pow(u_error, 2))/2.0), _threshold(threshold) {}

    
        // returns fpi by default or provided value if v >=0 
        float operator()(float i, float q, float u) {
            if (!std::isnan(i) && !std::isnan(q) && !std::isnan(u)) {
               float fpi = (float)(100.0 * std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term) / i);
               if (std::isnan(fpi) || (!std::isnan(_threshold) && (fpi < _threshold))) {
                  return FLOAT_NAN;
               }               
               return fpi;
            }
            
            return FLOAT_NAN;
        }

        // returns provided value:
        float operator()(float i, float q, float u, float v, bool threshold_v ) {
            if ( threshold_v ) {
               // v is a value to check against the threshold, otherwise a return value if threshold is applied to fpi (in else):
               float fpi = (float)(100.0 * std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term) / i);
               if (std::isnan(_threshold)) {
                  // ignore threshold if it is NaN
                  return fpi;
               }
               
               // was also !std::isnan(_threshold) &&
               if (!std::isnan(v) && (v >= _threshold) && !std::isnan(q) && !std::isnan(u) && !std::isnan(i)) {
                  return fpi;
               }              
            } else {
               // v is a return value when fPi >= threshold (in this case fPi is calculated as a threshold to display another value):
               if (!std::isnan(i) && !std::isnan(q) && !std::isnan(u)) {
                  float fpi = (float)(100.0 * std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term) / i);
                  if (std::isnan(fpi) || (!std::isnan(_threshold) && (fpi < _threshold))) {
                     return FLOAT_NAN;
                  }               
                  return v;
               }
            }
            
            return FLOAT_NAN;
        }

        
/*        float operator()(float i, float q, float u, float t) {
           if (!std::isnan(t) && !std::isnan(_threshold) && (t >= _threshold) && !std::isnan(q) && !std::isnan(u) && !std::isnan(i)) {
              return (float)(100.0 * std::sqrt(std::pow(q, 2) + std::pow(u, 2) - _error_term) / i);              
           }
           return FLOAT_NAN;
        }*/
    };

    struct CalcPa {
        const double _threshold;

        CalcPa(double threshold) : _threshold(threshold) {}

        float operator()(float q, float u) {
            if (!std::isnan(q) && !std::isnan(u)) {
               return (float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2; // TODO : try to optimise by re-orderign the operations for MINIMUM ERROR !
            }
            return FLOAT_NAN;
        }

        float operator()(float q, float u, float t) {
            if (!std::isnan(t) && !std::isnan(_threshold) && (t >= _threshold) && !std::isnan(q) && !std::isnan(u)) {
               return (float)(180.0 / casacore::C::pi) * std::atan2(u, q) / 2; // TODO : try to optimise by re-orderign the operations for MINIMUM ERROR !
            }
            return FLOAT_NAN;
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
