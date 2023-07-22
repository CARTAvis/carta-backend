/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentCalculator.h"
#include "ImageGenerator.h"
#include "Logger/Logger.h"

using namespace carta;

MomentCalculator::MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, const std::vector<int>& moment_types)
    : _image(image), _moment_types(moment_types) {
    _coord_sys = _image->coordinates();
    _spectral_coord = _coord_sys.spectralCoordinate();
}

void MomentCalculator::DoCalculation(float* data, size_t length, std::unordered_map<int, float>& results) {
    double sum(0);
    double sum_iv(0);
    size_t counts(0);
    for (size_t i = 0; i < length; ++i) {
        if (!std::isnan(data[i])) {
            sum += data[i];
            sum_iv += data[i] * GetVelocity(i);
            counts++;
        }
    }

    results[0] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : sum / (double)counts;
    results[1] = counts == 0 ? std::numeric_limits<double>::quiet_NaN() : GetDeltaVelocity() * sum;
    results[2] = sum_iv / sum;
}

double MomentCalculator::GetDeltaVelocity() {
    casacore::Quantum<casacore::Double> vel0;
    casacore::Quantum<casacore::Double> vel1;
    casacore::Double pix0 = _spectral_coord.referencePixel()(0) - 0.5;
    casacore::Double pix1 = _spectral_coord.referencePixel()(0) + 0.5;
    _spectral_coord.pixelToVelocity(vel0, pix0);
    _spectral_coord.pixelToVelocity(vel1, pix1);
    return abs(vel1.getValue() - vel0.getValue());
}

double MomentCalculator::GetVelocity(double chan) {
    casacore::Quantum<casacore::Double> vel;
    _spectral_coord.pixelToVelocity(vel, chan);
    return vel.getValue();
}
