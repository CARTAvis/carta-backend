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

class Deconvolver {
public:
    Deconvolver() = delete;

    Deconvolver(casacore::CoordinateSystem coord_sys, casacore::Unit brightness_unit, casacore::GaussianBeam beam, int stokes);

    ~Deconvolver(){};

    bool DoDeconvolution(const CARTA::GaussianComponent& in_gauss, std::shared_ptr<casa::GaussianShape>& out_gauss);

private:
    double CorrelatedOverallSNR(casacore::Quantity major, casacore::Quantity minor, double a, double b);
    casacore::Quantity GetNoiseFWHM();
    double GetResidueRms();

    casacore::CoordinateSystem _coord_sys;
    casacore::Unit _brightness_unit;
    casacore::GaussianBeam _beam;
    int _stokes;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
