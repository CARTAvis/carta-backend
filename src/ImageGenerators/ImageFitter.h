#ifndef IMAGEANALYSIS_IMAGEFITTER_H
#define IMAGEANALYSIS_IMAGEFITTER_H

#include <imageanalysis/ImageAnalysis/ImageTask.h>

#include <components/ComponentModels/ComponentList.h>
#include <lattices/LatticeMath/Fit2D.h>

#include <imageanalysis/IO/ImageFitterResults.h>

template <class T> class ImageFitter : public casa::ImageTask<T> {
public:
    ImageFitter() = delete;

    ImageFitter(
        const SPCIIT image, const casacore::String& region,
        const casacore::Record *const &regionRec,
        const casacore::String& box="", const casacore::String& chanInp="",
        const casacore::String& stokes="", const casacore::String& maskInp="",
        const casacore::String& estimates="",
        const casacore::String& newEstimatesInp="",
        const casacore::String& compListName=""
    );

    ImageFitter(const ImageFitter& other) = delete;

    // destructor
    virtual ~ImageFitter();

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

    casacore::String getResultsString() const {return _resultsString; }

protected:

    virtual casacore::Bool _hasLogfileSupport() const { return true; }

    virtual inline casacore::Bool _supportsMultipleRegions() const {
        return true;
    }

private:

    casacore::String _resultsString{};

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

    void _finishConstruction(const casacore::String& estimates);

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

#include "ImageFitter.tcc"

#endif