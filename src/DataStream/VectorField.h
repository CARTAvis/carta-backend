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

#define FLOAT_NAN std::numeric_limits<float>::quiet_NaN()

namespace carta {

class VectorField {
public:
    VectorField();

    bool SetParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis);
    bool ClearSets(const std::function<void(CARTA::VectorOverlayTileData&)>& callback, int z_index);

    int Mip() const {
        return _sets.smoothing_factor;
    }
    bool Fractional() const {
        return _sets.fractional;
    }
    float Threshold() const {
        return _sets.threshold;
    }
    bool CalculatePi() const {
        return _sets.calculate_pi;
    }
    bool CalculatePa() const {
        return _sets.calculate_pa;
    }
    bool CurrStokesAsPi() const {
        return _sets.current_stokes_as_pi;
    }
    bool CurrStokesAsPa() const {
        return _sets.current_stokes_as_pa;
    }

    void CalculatePiPa(std::unordered_map<std::string, std::vector<float>>& stokes_data, std::unordered_map<std::string, bool>& stokes_flag,
        const Tile& tile, int width, int height, int z_index, double progress,
        const std::function<void(CARTA::VectorOverlayTileData&)>& callback);

protected:
    struct VectorFieldSettings {
        int file_id;
        int smoothing_factor;
        bool fractional;
        double threshold;
        bool debiasing;
        double q_error;
        double u_error;
        int stokes_intensity;
        int stokes_angle;
        CARTA::CompressionType compression_type;
        float compression_quality;

        // Extra variables to be determined based on the existence of stokes axis
        bool calculate_pi;
        bool calculate_pa;
        bool current_stokes_as_pi;
        bool current_stokes_as_pa;

        VectorFieldSettings() : calculate_pi(false), calculate_pa(false), current_stokes_as_pi(false), current_stokes_as_pa(false) {
            ClearSettings();
        }

        VectorFieldSettings(const CARTA::SetVectorOverlayParameters& message, int stokes_axis = -1) {
            file_id = (int)message.file_id();
            smoothing_factor = (int)message.smoothing_factor();
            fractional = message.fractional();
            threshold = message.threshold();
            debiasing = message.debiasing();
            q_error = message.debiasing() ? message.q_error() : 0;
            u_error = message.debiasing() ? message.u_error() : 0;
            stokes_intensity = message.stokes_intensity();
            stokes_angle = message.stokes_angle();
            compression_type = message.compression_type();
            compression_quality = message.compression_quality();

            calculate_pi = stokes_intensity == 1 && stokes_axis > -1;
            calculate_pa = stokes_angle == 1 && stokes_axis > -1;
            current_stokes_as_pi = (stokes_intensity == 0 && stokes_axis > -1) || stokes_axis < 0;
            current_stokes_as_pa = (stokes_angle == 0 && stokes_axis > -1) || stokes_axis < 0;
        }

        // Equality operator for checking if vector field settings have changed
        bool operator==(const VectorFieldSettings& rhs) const {
            return (file_id == rhs.file_id && smoothing_factor == rhs.smoothing_factor && fractional == rhs.fractional &&
                    threshold == rhs.threshold && debiasing == rhs.debiasing && q_error == rhs.q_error && u_error == rhs.u_error &&
                    stokes_intensity == rhs.stokes_intensity && stokes_angle == rhs.stokes_angle &&
                    compression_type == rhs.compression_type && compression_quality == rhs.compression_quality);
        }

        bool operator!=(const VectorFieldSettings& rhs) const {
            return !(*this == rhs);
        }

        void ClearSettings() {
            file_id = -1;
            smoothing_factor = 0;
            fractional = false;
            threshold = std::numeric_limits<double>::quiet_NaN();
            debiasing = false;
            q_error = 0;
            u_error = 0;
            stokes_intensity = -1;
            stokes_angle = -1;
            compression_type = CARTA::CompressionType::NONE;
            compression_quality = 0;
        }
    };

private:
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

    VectorFieldSettings _sets;
};

void GetTiles(int image_width, int image_height, int mip, std::vector<carta::Tile>& tiles);

void FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width, int32_t tile_height,
    std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality);

CARTA::ImageBounds GetImageBounds(const carta::Tile& tile, int image_width, int image_height, int mip);

} // namespace carta

#endif // CARTA_BACKEND__FRAME_VECTORFIELD_H_
