/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_DATASTREAM_VECTORFIELD_H_
#define CARTA_SRC_DATASTREAM_VECTORFIELD_H_

#include <shared_mutex>
#include <memory>
#include <list>

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <casacore/casa/BasicSL/Constants.h>

#include "DataStream/Compression.h"
#include "DataStream/Tile.h"
#include "Util/Image.h"
#include "Util/Nan.h"

namespace carta {

using tile_callback_func = const std::function<bool(std::vector<float>&, CARTA::ImageBounds&, int, CARTA::PolarizationType, int&, int& )>; // was & 

class VectorFieldCalculator {
public:
    VectorFieldCalculator();
    
    VectorFieldCalculator(const CARTA::SetVectorOverlayParameters& message, int stokes_axis);

    bool CalculateVectorField(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims, tile_callback_func tile_callback);
    
    // check if calculation is still valid :
    bool IsValid() {
        return _is_valid;
    }
    
    void Invalidate() {
       _is_valid = false;
    }
    
    static bool Equivalent( const CARTA::SetVectorOverlayParameters& message1 , const CARTA::SetVectorOverlayParameters& message2 );

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
    void ClearSettings();
    void RenewParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis);
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

// This is a manger class managing VectorFieldCalculator objects creation, calculations and cancellations of ongoing calculations
class VectorField {
public :
    VectorField() {};
    
    bool SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message);
    bool CalculateVectorField(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims, AxesInfo& axes, tile_callback_func tile_callback);
    
    // invalidate all VectorFieldCalculators in the list:
    void Invalidate();

protected:
   // Vector field settings
   CARTA::SetVectorOverlayParameters _vector_field_request_message;
   std::mutex  _vector_field_mutex;
   std::list<std::shared_ptr<VectorFieldCalculator>> _vector_fields; // TBD/TODO : list or vector - depends if we need to delete elements in the middle (list may be better for this)
};


} // namespace carta

#endif // CARTA_SRC_DATASTREAM_VECTORFIELD_H_
