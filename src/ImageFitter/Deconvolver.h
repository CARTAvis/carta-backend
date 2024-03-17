/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_

#include "Util/Message.h"

#include <casacore/casa/Quanta.h>
#include <casacore/coordinates/Coordinates.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

#include <casacode/components/ComponentModels/GaussianShape.h>

namespace carta {

struct DeconvolutionResult {
    casacore::Quantity major;
    casacore::Quantity minor;
    casacore::Quantity pa;
    casacore::Quantity major_err;
    casacore::Quantity minor_err;
    casacore::Quantity pa_err;
};

class Deconvolver {
public:
    Deconvolver() = delete;
    Deconvolver(
        casacore::CoordinateSystem coord_sys, casacore::Unit brightness_unit, casacore::GaussianBeam beam, int stokes, double residue_rms);

    ~Deconvolver() = default;

    std::string GetDeconvolutionLog(const std::vector<CARTA::GaussianComponent>& in_gauss_vec);
    bool DoDeconvolution(const CARTA::GaussianComponent& in_gauss, DeconvolutionResult& result);
    bool WorldWidthToPixel(
        casacore::Quantity major, casacore::Quantity minor, casacore::Quantity pa, casacore::Vector<casacore::Double>& pixel_params);

private:
    double CorrelatedOverallSNR(double peak_intensities, casacore::Quantity major, casacore::Quantity minor, double a, double b);

    casacore::CoordinateSystem _coord_sys;
    casacore::Unit _brightness_unit;
    casacore::GaussianBeam _beam;
    int _stokes;
    double _residue_rms;
    casacore::Quantity _noise_FWHM;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
