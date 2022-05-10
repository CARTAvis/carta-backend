/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_VECTORFIELDSETTINGS_H_
#define CARTA_BACKEND__FRAME_VECTORFIELDSETTINGS_H_

#include <cmath>

#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>

struct VectorFieldSettings {
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

    VectorFieldSettings() {
        ClearSettings();
    }

    VectorFieldSettings(const CARTA::SetVectorOverlayParameters& message) {
        smoothing_factor = (int)message.smoothing_factor();
        fractional = message.fractional();
        threshold = message.threshold();
        debiasing = message.debiasing();
        q_error = message.q_error();
        u_error = message.u_error();
        stokes_intensity = message.stokes_intensity();
        stokes_angle = message.stokes_angle();
        compression_type = message.compression_type();
        compression_quality = message.compression_quality();
    }

    // Equality operator for checking if vector field settings have changed
    bool operator==(const VectorFieldSettings& rhs) const {
        return (this->smoothing_factor == rhs.smoothing_factor && this->fractional == rhs.fractional && this->threshold == rhs.threshold &&
                this->debiasing == rhs.debiasing && this->q_error == rhs.q_error && this->u_error == rhs.u_error &&
                this->stokes_intensity == rhs.stokes_intensity && this->stokes_angle == rhs.stokes_angle &&
                this->compression_type == rhs.compression_type && this->compression_quality == rhs.compression_quality);
    }

    bool operator!=(const VectorFieldSettings& rhs) const {
        return !(*this == rhs);
    }

    void ClearSettings() {
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

#endif // CARTA_BACKEND__FRAME_VECTORFIELDSETTINGS_H_
