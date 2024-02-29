//# Copyright (C) 1998,1999,2000,2001,2003
//# Associated Universities, Inc. Washington DC, USA.
//#
//# This program is free software; you can redistribute it and/or modify it
//# under the terms of the GNU General Public License as published by the Free
//# Software Foundation; either version 2 of the License, or (at your option)
//# any later version.
//#
//# This program is distributed in the hope that it will be useful, but WITHOUT
//# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//# more details.
//#
//# You should have received a copy of the GNU General Public License along
//# with this program; if not, write to the Free Software Foundation, Inc.,
//# 675 Massachusetts Ave, Cambridge, MA 02139, USA.
//#
//# Correspondence concerning AIPS++ should be addressed as follows:
//#        Internet email: aips2-request@nrao.edu.
//#        Postal address: AIPS++ Project Office
//#                        National Radio Astronomy Observatory
//#                        520 Edgemont Road
//#                        Charlottesville, VA 22903-2475 USA
//#

#ifndef IMAGEANALYSIS_IMAGEFITTER_H
#define IMAGEANALYSIS_IMAGEFITTER_H

#include <imageanalysis/ImageAnalysis/ImageTask.h>

#include <components/ComponentModels/ComponentList.h>
#include <casacore/lattices/LatticeMath/Fit2D.h>

#include <imageanalysis/IO/ImageFitterResults.h>

namespace carta {

template <class T> class ImageFitter2 : public casa::ImageTask<T> {
    // <summary>
    // Top level interface to ImageAnalysis::fitsky to handle inputs, bookkeeping etc and
    // ultimately call fitsky to do fitting
    // </summary>

    // <reviewed reviewer="" date="" tests="" demos="">
    // </reviewed>

    // <prerequisite>
    // </prerequisite>

    // <etymology>
    // Fits components to sources in images (ImageSourceComponentFitter was deemed to be to long
    // of a name)
    // </etymology>

    // <synopsis>
    // ImageFitter is the top level interface for fitting image source components. It handles most
    // of the inputs, bookkeeping etc. It can be instantiated and its one public method, fit,
    // run from either a C++ app or python.
    // </synopsis>

    // <example>
    // <srcblock>
    // ImageFitter fitter(...)
    // fitter.fit()
    // </srcblock>
    // </example>

public:

    ImageFitter2() = delete;

    // constructor appropriate for API calls.
    // Parameters:
    // <ul>
    // <li>imagename - the name of the input image in which to fit the models</li>
    // <li>box - A 2-D rectangular box in which to use pixels for the fitting, eg box=100,120,200,230
    // In cases where both box and region are specified, box, not region, is used.</li>
    // <li>region - Named region to use for fitting</li>
    // <li>regionPtr - A pointer to a region. Note there are unfortunately several different types of
    // region records throughout CASA. In this case, it must be a casacore::Record produced by creating a
    // region via a casacore::RegionManager method.
    // <li>chanInp - Zero-based channel number on which to do the fit. Only a single channel can be
    // specified.</li>
    // <li>stokes - casacore::Stokes plane on which to do the fit. Only a single casacore::Stokes parameter can be
    // specified.</li>
    // <li> maskInp - Mask (as LEL) to use as a way to specify which pixels to use </li>
    // <li> includepix - Pixel value range to include in the fit. includepix and excludepix
    // cannot be specified simultaneously. </li>
    // <li> excludepix - Pixel value range to exclude from fit</li>
    // <li> residualInp - Name of residual image to save. Blank means do not save residual image</li>
    // <li> modelInp - Name of the model image to save. Blank means do not save model image</li>

    // use these constructors when you already have a pointer to a valid casacore::ImageInterface object


    ImageFitter2(
        const SPCIIT image, const casacore::String& region,
        const casacore::Record *const &regionRec,
        const casacore::String& box="", const casacore::String& chanInp="",
        const casacore::String& stokes="", const casacore::String& maskInp="",
        const casacore::String& estiamtesFilename="",
        const casacore::String& newEstimatesInp="",
        const casacore::String& compListName=""
    );

    ImageFitter2(const ImageFitter2& other) = delete;

    // destructor
    virtual ~ImageFitter2();

    // Do the fit. If componentList is specified, store the fitted components in
    // that object. The first list in the returned pair represents the convolved components.
    // The second list represents the deconvolved components. If the image has no beam,
    // the two lists will be the same.
    std::pair<casa::ComponentList, casa::ComponentList> fit();

    void setWriteControl(typename casa::ImageFitterResults<T>::CompListWriteControl x) {
        _writeControl = x;
    }

    inline casacore::String getClass() const {return _class;}

    // Did the fit converge for the specified channel?
    // Throw casacore::AipsError if the fit has not yet been done.
    // <src>plane</src> is relative to the first plane in the image chosen to be fit.
    casacore::Bool converged(casacore::uInt plane) const;

    // Did the fit converge?
    // Throw casacore::AipsError if the fit has not yet been done.
    // <src>plane</src> is relative to the first plane in the image chosen to be fit.
    casacore::Vector<casacore::Bool> converged() const;

    // set the zero level estimate. Implies fitting of zero level should be done. Must be
    // called before fit() to have an effect.
    void setZeroLevelEstimate(
        casacore::Double estimate, casacore::Bool isFixed
    );

    // Unset zero level (resets to zero). Implies fitting of zero level should not be done.
    // Call prior to fit().
    void unsetZeroLevelEstimate();

    // get the fitted result and error. Throws
    // an exception if the zero level was not fit for.
    void getZeroLevelSolution(
        std::vector<casacore::Double>& solution, std::vector<casacore::Double>& error
    );

    // set rms level for calculating uncertainties. If not positive, an exception is thrown.
    void setRMS(const casacore::Quantity& rms);

    void setIncludePixelRange(const std::pair<T, T>& r) {
        _includePixelRange.reset(new std::pair<T, T>(r));
    }

    void setExcludePixelRange(const std::pair<T, T>& r) {
        _excludePixelRange.reset(new std::pair<T, T>(r));
    }

    // set the output model image name
    void setModel(const casacore::String& m) { _model = m; }

    // set the output residual image name
    void setResidual(const casacore::String& r) { _residual = r; }

    // set noise correlation beam FWHM
    void setNoiseFWHM(const casacore::Quantity& q);

    // in pixel widths
    void setNoiseFWHM(casacore::Double d);

    // clear noise FWHM, if the image has no beam, use the uncorrelated noise equations.
    // If the image has a beam(s) use the correlated noise equations with theta_N =
    // the geometric mean of the beam major and minor axes.
    void clearNoiseFWHM();

    // The casacore::Record holding all the output info
    casacore::Record getOutputRecord() const {return _output; }

    // Set The summary text file name.
    void setSummaryFile(const casacore::String& f) { _summary = f; }

protected:

    virtual casacore::Bool _hasLogfileSupport() const { return true; }

    virtual inline casacore::Bool _supportsMultipleRegions() const {
        return true;
    }

private:

    using Angular2DGaussian = casacore::GaussianBeam;

    casacore::String _regionString{};
    casacore::String _residual{}, _model{}, _estimatesString{}, _summary{};
    casacore::String _newEstimatesFileName, _compListName, _bUnit;
    std::shared_ptr<std::pair<T, T>> _includePixelRange{}, _excludePixelRange{};
    casa::ComponentList _estimates{}, _curConvolvedList, _curDeconvolvedList;
    casacore::Vector<casacore::String> _fixed{}, _deconvolvedMessages;
    casacore::Bool _fitDone{false}, _noBeam{false}, _doZeroLevel{false},
        _zeroLevelIsFixed{false}, _correlatedNoise, _useBeamForNoise;
    casacore::Vector<casacore::Bool> _fitConverged{};
    std::vector<casacore::Quantity> _peakIntensities{}, _peakIntensityErrors{},
        _fluxDensityErrors{}, _fluxDensities{}, _majorAxes{},
        _majorAxisErrors{}, _minorAxes{}, _minorAxisErrors{}, _positionAngles{},
        _positionAngleErrors{};
    std::vector<casacore::Quantity> _allConvolvedPeakIntensities{},
        _allConvolvedPeakIntensityErrors{}, _allSums{}, _allFluxDensities{},
        _allFluxDensityErrors{};
    std::vector<std::shared_ptr<casacore::Vector<casacore::Double>>>
        _pixelCoords{};
    std::vector<casacore::GaussianBeam> _allBeams;
    std::vector<casacore::Double> _allBeamsPix, _allBeamsSter;
    std::vector<casacore::uInt> _allChanNums;
    std::vector<casacore::Bool> _isPoint;
    casacore::Record _residStats, inputStats, _output;
    casacore::Double _rms = -1;
    casacore::String _kludgedStokes;
    typename casa::ImageFitterResults<T>::CompListWriteControl _writeControl{
        casa::ImageFitterResults<T>::NO_WRITE
    };
    casacore::Vector<casacore::uInt> _chanVec;
    casacore::uInt _curChan;
    casacore::Double _zeroLevelOffsetEstimate = 0;
    std::vector<casacore::Double> _zeroLevelOffsetSolution,
        _zeroLevelOffsetError;
    casacore::Int _stokesPixNumber = -1, _chanPixNumber = -1;
    casa::ImageFitterResults<T> _results;
    std::unique_ptr<casacore::Quantity> _noiseFWHM{};
    casacore::Quantity _pixWidth{0, "arcsec"};

    const static casacore::String _class;

    void _fitLoop(
        casacore::Bool& anyConverged, casa::ComponentList& convolvedList,
        casa::ComponentList& deconvolvedList, SPIIT templateImage,
        SPIIT residualImage, SPIIT modelImage, casacore::String& resultsString
    );

    std::vector<casa::OutputDestinationChecker::OutputStruct> _getOutputStruct();

    std::vector<casacore::Coordinate::Type> _getNecessaryCoordinates() const;

    casa::CasacRegionManager::StokesControl _getStokesControl() const;

    void _finishConstruction(const casacore::String& estimatesFilename);

    // summarize the results in a nicely formatted string
    casacore::String _resultsToString(casacore::uInt nPixels);

    //summarize the size details in a nicely formatted string
    casacore::String _sizeToString(const casacore::uInt compNumber) const;

    casacore::String _spectrumToString(casacore::uInt compNumber) const;

    void _setDeconvolvedSizes();

    void _getStandardDeviations(
        casacore::Double& inputStdDev, casacore::Double& residStdDev
    ) const;

    void _getRMSs(casacore::Double& inputRMS, casacore::Double& residRMS) const;

    casacore::Double _getStatistic(
        const casacore::String& type, const casacore::uInt index,
        const casacore::Record& stats
    ) const;

    casacore::String _statisticsToString() const;

    SPIIT _createImageTemplate() const;

    void _writeCompList(casa::ComponentList& list) const;

    void _setIncludeExclude(casacore::Fit2D& fitter) const;

    void _fitsky(
        casacore::Fit2D& fitter, casacore::Array<T>& pixels,
        casacore::Array<casacore::Bool>& pixelMask, casacore::Bool& converged,
        casacore::Double& zeroLevelOffsetSolution,
        casacore::Double& zeroLevelOffsetError,
        std::pair<casacore::Int, casacore::Int>& pixelOffsets,
        const casacore::Vector<casacore::String>& models, casacore::Bool fitIt,
        casacore::Bool deconvolveIt,
        casacore::Double zeroLevelEstimate
    );

    casacore::Vector<casacore::Double> _singleParameterEstimate(
        casacore::Fit2D& fitter, casacore::Fit2D::Types model,
        const casacore::MaskedArray<T>& pixels, T minVal, T maxVal,
        const casacore::IPosition& minPos, const casacore::IPosition& maxPos
    ) const;

    casa::ComponentType::Shape _convertModelType(casacore::Fit2D::Types typeIn) const;

    void _fitskyExtractBeam(
        casacore::Vector<casacore::Double>& parameters,
        const casacore::ImageInfo& imageInfo, const casacore::Bool xIsLong,
        const casacore::CoordinateSystem& cSys
    ) const;

    void _encodeSkyComponentError(
        casa::SkyComponent& sky, casacore::Double facToJy,
        const casacore::CoordinateSystem& csys,
        const casacore::Vector<casacore::Double>& parameters,
        const casacore::Vector<casacore::Double>& errors,
        casacore::Stokes::StokesTypes stokes, casacore::Bool xIsLong
    ) const;

    void _doConverged(
        casa::ComponentList& convolvedList, casa::ComponentList& deconvolvedList,
        casacore::Double& zeroLevelOffsetEstimate,
        std::pair<casacore::Int, casacore::Int>& pixelOffsets,
        SPIIT& residualImage, SPIIT& modelImage,
        std::shared_ptr<casacore::TempImage<T>>& tImage,
        std::shared_ptr<casacore::ArrayLattice<casacore::Bool> >& initMask,
        casacore::Double zeroLevelOffsetSolution,
        casacore::Double zeroLevelOffsetError, casacore::Bool hasSpectralAxis,
        casacore::Int spectralAxisNumber, casacore::Bool outputImages,
        const casacore::IPosition& planeShape,
        const casacore::Array<T>& pixels,
        const casacore::Array<casacore::Bool>& pixelMask,
        const casacore::Fit2D& fitter
    );

    casacore::Quantity _pixelWidth();

    void _calculateErrors();

    casacore::Double _getRMS() const;

    casacore::Double _correlatedOverallSNR(
        casacore::uInt comp, casacore::Double a, casacore::Double b,
        casacore::Double signalToNoise
    ) const;

    casacore::GaussianBeam _getCurrentBeam() const;

    void _createOutputRecord(
        const casa::ComponentList& convolved, const casa::ComponentList& decon
    );

    void _setSum(
        const casa::SkyComponent& comp, const casacore::SubImage<T>& im,
        casacore::uInt compNum
    );

    void _setBeam(casacore::GaussianBeam& beam, casacore::uInt ngauss);
};

}

#ifndef AIPS_NO_TEMPLATE_SRC
#include "ImageFitter2.tcc"
#endif

#endif
