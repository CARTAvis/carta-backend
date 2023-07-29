/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_VECTORFIELD_H_
#define CARTA_BACKEND__FRAME_VECTORFIELD_H_

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <casacore/casa/BasicSL/Constants.h>

#include "DataStream/Compression.h"
#include "DataStream/Tile.h"
#include "Util/Image.h"

namespace carta {

class VectorField {
public:
    VectorField();

    bool SetParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis);
    bool ClearParameters(const std::function<void(CARTA::VectorOverlayTileData&)>& callback, int z_index);

    void CalculatePiPa(std::unordered_map<std::string, std::vector<float>>& stokes_data, std::unordered_map<std::string, bool>& stokes_flag,
        const Tile& tile, int width, int height, int z_index, double progress,
        const std::function<void(CARTA::VectorOverlayTileData&)>& callback);

    int Mip() const {
        return _smoothing_factor;
    }
    bool Fractional() const {
        return _fractional;
    }
    float Threshold() const {
        return _threshold;
    }
    bool CalculatePi() const {
        return _calculate_pi;
    }
    bool CalculatePa() const {
        return _calculate_pa;
    }
    bool CurrStokesAsPi() const {
        return _current_stokes_as_pi;
    }
    bool CurrStokesAsPa() const {
        return _current_stokes_as_pa;
    }

protected:
    void ClearSettings();
    bool IsEqual(const CARTA::SetVectorOverlayParameters& message);
    void RenewParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis);
    void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
        std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);

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

    // Extra variables to be determined based on the existence of stokes axis
    bool _calculate_pi;
    bool _calculate_pa;
    bool _current_stokes_as_pi;
    bool _current_stokes_as_pa;
};

void GetTiles(int image_width, int image_height, int mip, std::vector<carta::Tile>& tiles);

CARTA::ImageBounds GetImageBounds(const carta::Tile& tile, int image_width, int image_height, int mip);

} // namespace carta

#endif // CARTA_BACKEND__FRAME_VECTORFIELD_H_
