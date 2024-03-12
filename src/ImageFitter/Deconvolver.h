/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_

#include "Util/Message.h"

#include <casacore/casa/Quanta.h>

#include <casacode/components/ComponentModels/GaussianShape.h>
#include <casacode/imageanalysis/ImageTypedefs.h>

namespace carta {

class Deconvolver {
public:
    Deconvolver() = delete;

    Deconvolver(casa::SPIIF image);

    ~Deconvolver(){};

    bool DoDeconvolution(int chan, int stokes, const CARTA::GaussianComponent& in_gauss, std::shared_ptr<casa::GaussianShape>& out_gauss);

private:
    double CorrelatedOverallSNR(int chan, int stokes, casacore::Quantity major, casacore::Quantity minor, double a, double b);
    casacore::Quantity GetNoiseFWHM(int chan, int stokes);
    double GetResidueRms();

    casa::SPIIF _image;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
