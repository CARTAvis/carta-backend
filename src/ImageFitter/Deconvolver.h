/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_

#include "Util/Message.h"

#include <casacore/lattices/LatticeMath/Fit2D.h>

#include <casacode/components/ComponentModels/ComponentList.h>
#include <casacode/imageanalysis/IO/ImageFitterResults.h>
#include <casacode/imageanalysis/ImageAnalysis/ImageTask.h>

namespace carta {

template <class T>
class Deconvolver : public casa::ImageTask<T> {
public:
    Deconvolver() = delete;

    Deconvolver(const SPCIIT image, const casacore::String& region, const casacore::Record* const& regionRec,
        const casacore::String& box = "", const casacore::String& chanInp = "", const casacore::String& stokes = "",
        const casacore::String& maskInp = "", const casacore::String& estiamtesFilename = "", const casacore::String& newEstimatesInp = "",
        const casacore::String& compListName = "");

    ~Deconvolver(){};

    inline casacore::String getClass() const {
        return _class;
    }

    bool DoDeconvolution(const CARTA::GaussianComponent& in_gauss, std::shared_ptr<casa::GaussianShape>& out_gauss);

private:
    double GetResidueRms();
    casacore::Quantity GetNoiseFWHM();
    double CorrelatedOverallSNR(Quantity major, Quantity minor, double a, double b);

    casa::CasacRegionManager::StokesControl _getStokesControl() const;
    std::vector<casacore::Coordinate::Type> _getNecessaryCoordinates() const;

    const static casacore::String _class;
};

} // namespace carta

#ifndef AIPS_NO_TEMPLATE_SRC
#include "Deconvolver.tcc"
#endif

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
