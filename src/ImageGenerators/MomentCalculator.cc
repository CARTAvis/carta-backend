/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentCalculator.h"
#include "ImageGenerator.h"
#include "Logger/Logger.h"

#include <coordinates/Coordinates/SpectralCoordinate.h>

using namespace carta;

MomentCalculator::MomentCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, const std::vector<int>& moment_types)
    : _image(image), _moment_types(moment_types) {}

void MomentCalculator::DoCalculation(float* data, size_t length, std::unordered_map<int, float>& results) {
    double sum(0);
    size_t counts(0);
    for (size_t i = 0; i < length; ++i) {
        if (!std::isnan(data[i])) {
            sum += data[i];
            counts++;
        }
    }

    results[0] = counts == 0 ? std::numeric_limits<float>::quiet_NaN() : sum / (double)counts;
    results[1] = counts == 0 ? std::numeric_limits<float>::quiet_NaN() : GetDeltaV() * sum;
}

double MomentCalculator::GetDeltaV() {
    casacore::CoordinateSystem coord_sys = _image->coordinates();
    casacore::SpectralCoordinate spectral_coord = coord_sys.spectralCoordinate();
    casacore::Quantum<casacore::Double> vel0;
    casacore::Quantum<casacore::Double> vel1;
    casacore::Double pix0 = spectral_coord.referencePixel()(0) - 0.5;
    casacore::Double pix1 = spectral_coord.referencePixel()(0) + 0.5;
    spectral_coord.pixelToVelocity(vel0, pix0);
    spectral_coord.pixelToVelocity(vel1, pix1);
    return abs(vel1.getValue() - vel0.getValue());
}
