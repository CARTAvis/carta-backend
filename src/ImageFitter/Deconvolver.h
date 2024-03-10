// # Copyright (C) 1998,1999,2000,2001,2003
// # Associated Universities, Inc. Washington DC, USA.
// #
// # This program is free software; you can redistribute it and/or modify it
// # under the terms of the GNU General Public License as published by the Free
// # Software Foundation; either version 2 of the License, or (at your option)
// # any later version.
// #
// # This program is distributed in the hope that it will be useful, but WITHOUT
// # ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// # FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// # more details.
// #
// # You should have received a copy of the GNU General Public License along
// # with this program; if not, write to the Free Software Foundation, Inc.,
// # 675 Massachusetts Ave, Cambridge, MA 02139, USA.
// #
// # Correspondence concerning AIPS++ should be addressed as follows:
// #        Internet email: aips2-request@nrao.edu.
// #        Postal address: AIPS++ Project Office
// #                        National Radio Astronomy Observatory
// #                        520 Edgemont Road
// #                        Charlottesville, VA 22903-2475 USA
// #

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

    ~Deconvolver();

    inline casacore::String getClass() const {
        return _class;
    }

    bool DoDeconvolution(const CARTA::GaussianComponent& in_gauss);

private:
    double GetResidueRms();
    casacore::Quantity GetNoiseFWHM();
    double CorrelatedOverallSNR(Quantity maj, Quantity min, double a, double b);

    casacore::String _regionString{};
    casacore::String _residual{}, _model{}, _estimatesString{}, _summary{};
    casacore::String _newEstimatesFileName, _compListName, _bUnit;
    std::shared_ptr<std::pair<T, T>> _includePixelRange{}, _excludePixelRange{};
    casa::ComponentList _estimates{}, _curConvolvedList, _curDeconvolvedList;
    casacore::Vector<casacore::String> _fixed{}, _deconvolvedMessages;
    casacore::Bool _fitDone{false}, _noBeam{false}, _doZeroLevel{false}, _zeroLevelIsFixed{false}, _correlatedNoise, _useBeamForNoise;
    casacore::Vector<casacore::Bool> _fitConverged{};
    std::vector<casacore::Quantity> _peakIntensities{}, _peakIntensityErrors{}, _fluxDensityErrors{}, _fluxDensities{}, _majorAxes{},
        _majorAxisErrors{}, _minorAxes{}, _minorAxisErrors{}, _positionAngles{}, _positionAngleErrors{};
    std::vector<casacore::Quantity> _allConvolvedPeakIntensities{}, _allConvolvedPeakIntensityErrors{}, _allSums{}, _allFluxDensities{},
        _allFluxDensityErrors{};
    std::vector<std::shared_ptr<casacore::Vector<casacore::Double>>> _pixelCoords{};
    std::vector<casacore::GaussianBeam> _allBeams;
    std::vector<casacore::Double> _allBeamsPix, _allBeamsSter;
    std::vector<casacore::uInt> _allChanNums;
    std::vector<casacore::Bool> _isPoint;
    casacore::Record _residStats, inputStats, _output;
    casacore::Double _rms = -1;
    casacore::String _kludgedStokes;
    typename casa::ImageFitterResults<T>::CompListWriteControl _writeControl{casa::ImageFitterResults<T>::NO_WRITE};
    casacore::Vector<casacore::uInt> _chanVec;
    casacore::uInt _curChan;
    casacore::Double _zeroLevelOffsetEstimate = 0;
    std::vector<casacore::Double> _zeroLevelOffsetSolution, _zeroLevelOffsetError;
    casacore::Int _stokesPixNumber = -1, _chanPixNumber = -1;
    casa::ImageFitterResults<T> _results;
    std::unique_ptr<casacore::Quantity> _noiseFWHM{};
    casacore::Quantity _pixWidth{0, "arcsec"};

    const static casacore::String _class;

    casa::CasacRegionManager::StokesControl _getStokesControl() const;
    std::vector<casacore::Coordinate::Type> _getNecessaryCoordinates() const;
};

} // namespace carta

#ifndef AIPS_NO_TEMPLATE_SRC
#include "Deconvolver.tcc"
#endif

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_H_
