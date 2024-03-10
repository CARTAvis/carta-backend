/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_IMAGEFITTER2_TCC_
#define CARTA_SRC_IMAGEFITTER_IMAGEFITTER2_TCC_

#include <imageanalysis/ImageAnalysis/ImageFitter.h>

#include <casacore/casa/BasicSL/STLIO.h>
#include <casacore/casa/Utilities/Precision.h>
#include <components/ComponentModels/ComponentShape.h>
#include <components/ComponentModels/Flux.h>
#include <components/ComponentModels/GaussianDeconvolver.h>
#include <components/ComponentModels/GaussianShape.h>
#include <components/ComponentModels/PointShape.h>
#include <components/ComponentModels/SkyComponentFactory.h>
#include <components/ComponentModels/SpectralModel.h>

#include <casacore/lattices/LRegions/LCPixelSet.h>

#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/IO/FitterEstimatesFileParser.h>
#include <imageanalysis/ImageAnalysis/ImageStatsCalculator.h>
#include <imageanalysis/ImageAnalysis/PeakIntensityFluxDensityConverter.h>

using namespace casacore;
namespace carta {

template <class T>
const String ImageFitter2<T>::_class = "ImageFitter";

template <class T>
ImageFitter2<T>::ImageFitter2(const SPCIIT image, const String& region, const Record* const& regionRec, const String& box,
    const String& chanInp, const String& stokes, const String& maskInp, const String& estimatesFilename, const String& newEstimatesInp,
    const String& compListName)
    : casa::ImageTask<T>(image, region, regionRec, box, chanInp, stokes, maskInp, "", false),
      _regionString(region),
      _newEstimatesFileName(newEstimatesInp),
      _compListName(compListName),
      _bUnit(image->units().getName()),
      _correlatedNoise(image->imageInfo().hasBeam()),
      _zeroLevelOffsetSolution(0),
      _zeroLevelOffsetError(0),
      _results(image, this->_getLog()) {
    if (stokes.empty() && image->coordinates().hasPolarizationCoordinate() && regionRec == 0 && region.empty()) {
        const CoordinateSystem& csys = image->coordinates();
        Int polAxis = csys.polarizationAxisNumber();
        Int stokesVal = (Int)csys.toWorld(IPosition(image->ndim(), 0))[polAxis];
        this->_setStokes(Stokes::name(Stokes::type(stokesVal)));
    }
    this->_construct();
    _finishConstruction(estimatesFilename);
}

template <class T>
ImageFitter2<T>::~ImageFitter2() {}

template <class T>
std::pair<casa::ComponentList, casa::ComponentList> ImageFitter2<T>::fit() {
    SPIIT modelImage, residualImage, templateImage;
    Bool doResid = !_residual.empty();
    Bool doModel = !_model.empty();
    if (doResid || doModel) {
        if (doResid) {
            residualImage = _createImageTemplate();
            templateImage = residualImage;
        }
        if (doModel) {
            modelImage = _createImageTemplate();
            templateImage = modelImage;
        }
    }
    uInt ngauss = _estimates.nelements() > 0 ? _estimates.nelements() : 1;
    Vector<String> models(ngauss, "gaussian");
    if (_doZeroLevel) {
        models.resize(ngauss + 1, true);
        models[ngauss] = "level";
        _fixed.resize(ngauss + 1, true);
        _fixed[ngauss] = _zeroLevelIsFixed ? "l" : "";
    }
    _useBeamForNoise = _correlatedNoise && !_noiseFWHM.get() && this->_getImage()->imageInfo().hasBeam();
    {
        // CAS-6971
        String msg;
        if (_noiseFWHM && !_correlatedNoise) {
            msg =
                "Specified noise FWHM is less than a pixel "
                "width, so uncertainties will be computed "
                "assuming uncorrelated pixel noise.";
        } else if (!_noiseFWHM) {
            if (this->_getImage()->imageInfo().hasBeam()) {
                msg =
                    "noise FWHM not specified, so uncertainties "
                    "will be computed using the beam geometric mean "
                    "FWHM as the pixel noise correlation FWHM";
            } else {
                msg =
                    "noise FWHM not specified and image has no "
                    "beam, so uncertainties will be computed "
                    "assuming pixel noise is uncorrelated.";
            }
        }
        *this->_getLog() << LogOrigin(getClass(), __func__) << LogIO::NORMAL << msg << LogIO::POST;
    }
    String errmsg;
    casa::ImageStatsCalculator<T> myStats(this->_getImage(), this->_getRegion(), "", false);
    myStats.setList(false);
    myStats.setVerbose(false);
    myStats.setAxes(this->_getImage()->coordinates().directionAxesNumbers());
    inputStats = myStats.statistics();
    Vector<String> allowFluxUnits(2, "Jy.km/s");
    allowFluxUnits[1] = "K.rad2";
    _results.setStokes(this->_getStokes());
    String resultsString = _results.resultsHeader(
        this->_getChans(), _chanVec, _regionString, this->_getMask(), _includePixelRange, _excludePixelRange, _estimatesString);
    LogOrigin origin(_class, __func__);
    *this->_getLog() << origin;
    *this->_getLog() << LogIO::NORMAL << resultsString << LogIO::POST;
    casa::ComponentList convolvedList, deconvolvedList;
    Bool anyConverged = false;
    Array<T> residPixels, modelPixels;
    _fitLoop(anyConverged, convolvedList, deconvolvedList, templateImage, residualImage, modelImage, resultsString);
    if (anyConverged) {
        _results.writeCompList(convolvedList, _compListName, _writeControl);
    } else if (!_compListName.empty()) {
        *this->_getLog() << LogIO::WARN << "No fits converged. Will not write component list" << LogIO::POST;
    }
    if (residualImage || modelImage) {
        if (residualImage) {
            try {
                this->_prepareOutputImage(*residualImage, 0, 0, 0, 0, &_residual, true);
            } catch (const AipsError& x) {
                *this->_getLog() << LogIO::WARN << "Error writing "
                                 << "residual image. The reported error is " << x.getMesg() << LogIO::POST;
            }
        }
        if (modelImage) {
            try {
                this->_prepareOutputImage(*modelImage, 0, 0, 0, 0, &_model, true);
            } catch (const AipsError& x) {
                *this->_getLog() << LogIO::WARN << "Error writing"
                                 << "model image. The reported error is " << x.getMesg() << LogIO::POST;
            }
        }
    }
    if (anyConverged && (!_newEstimatesFileName.empty() || !_summary.empty())) {
        if (!_newEstimatesFileName.empty()) {
            _results.setConvolvedList(_curConvolvedList);
            _results.setPeakIntensities(_peakIntensities);
            _results.setMajorAxes(_majorAxes);
            _results.setMinorAxes(_minorAxes);
            _results.setPositionAngles(_positionAngles);
            _results.writeNewEstimatesFile(_newEstimatesFileName);
        }
        if (!_summary.empty()) {
            _results.setConvolvedList(convolvedList);
            _results.setDeconvolvedList(deconvolvedList);
            _results.setChannels(_allChanNums);
            _results.setFluxDensities(_allFluxDensities);
            _results.setFluxDensityErrors(_allFluxDensityErrors);
            _results.setPeakIntensities(_allConvolvedPeakIntensities);
            _results.setPeakIntensityErrors(_allConvolvedPeakIntensityErrors);
            _results.writeSummaryFile(_summary, this->_getImage()->coordinates());
        }
    }
    _createOutputRecord(convolvedList, deconvolvedList);
    this->_writeLogfile(resultsString);
    std::pair<casa::ComponentList, casa::ComponentList> lists;
    lists.first = convolvedList;
    lists.second = deconvolvedList;
    return lists;
}

template <class T>
void ImageFitter2<T>::_createOutputRecord(const casa::ComponentList& convolved, const casa::ComponentList& decon) {
    String error;
    Record allConvolved, allDeconvolved;
    convolved.toRecord(error, allConvolved);
    Bool dodecon = decon.nelements() > 0;
    if (dodecon > 0) {
        decon.toRecord(error, allDeconvolved);
    }
    Bool addBeam = !_allBeams.empty();
    uInt n = convolved.nelements();
    for (uInt i = 0; i < n; ++i) {
        Record peak;
        peak.define("value", _allConvolvedPeakIntensities[i].getValue());
        String unit = _allConvolvedPeakIntensities[i].getUnit();
        peak.define("unit", unit);
        peak.define("error", _allConvolvedPeakIntensityErrors[i].getValue());
        String compString = "component" + String::toString(i);
        Record sub = allConvolved.asRecord(compString);
        sub.defineRecord("peak", peak);
        Record sum;
        sum.define("value", _allSums[i].getValue());
        sum.define("unit", _allSums[i].getUnit());
        sub.defineRecord("sum", sum);
        Record beam;
        if (addBeam) {
            beam.defineRecord("beamarcsec", _allBeams[i].toRecord());
            beam.define("beampixels", _allBeamsPix[i]);
            beam.define("beamster", _allBeamsSter[i]);
            sub.defineRecord("beam", beam);
        }
        Record spectrum = sub.asRecord("spectrum");
        spectrum.define("channel", _allChanNums[i]);
        sub.defineRecord("spectrum", spectrum);
        if (dodecon) {
            sub.define("ispoint", _isPoint[i]);
        }
        if (_pixelCoords[i]) {
            sub.define("pixelcoords", *(_pixelCoords)[i]);
        }
        allConvolved.defineRecord(compString, sub);
        if (dodecon) {
            Record sub1 = allDeconvolved.asRecord(compString);
            if (decon.getShape(i)->type() == casa::ComponentType::GAUSSIAN) {
                Double areaRatio = (static_cast<const casa::GaussianShape*>(convolved.getShape(i))->getArea() /
                                    static_cast<const casa::GaussianShape*>(decon.getShape(i))->getArea())
                                       .getValue("");
                if (areaRatio < 1e6) {
                    Record peak;
                    Double x = _allConvolvedPeakIntensities[i].getValue() * areaRatio;
                    peak.define("value", x);
                    String unit = _allConvolvedPeakIntensities[i].getUnit();
                    peak.define("unit", unit);
                    peak.define("error", _allConvolvedPeakIntensityErrors[i].getValue() * areaRatio);
                    sub1.defineRecord("peak", peak);
                }
                if (addBeam) {
                    sub1.defineRecord("beam", beam);
                }
            }
            sub1.defineRecord("sum", sum);
            Record spectrum = sub1.asRecord("spectrum");
            spectrum.define("channel", _allChanNums[i]);
            sub1.defineRecord("spectrum", spectrum);
            sub1.define("ispoint", _isPoint[i]);
            allDeconvolved.defineRecord(compString, sub1);
        }
    }
    _output.defineRecord("results", allConvolved);
    if (dodecon) {
        _output.defineRecord("deconvolved", allDeconvolved);
    }
    _output.define("converged", _fitConverged);
    const auto& dc = this->_getImage()->coordinates().directionCoordinate();
    auto inc = dc.increment();
    auto units = dc.worldAxisUnits();
    Vector<Double> pixelsPerArcsec(2);
    pixelsPerArcsec[0] = abs(1 / Quantity(inc[0], units[0]).getValue("arcsec"));
    pixelsPerArcsec[1] = abs(1 / Quantity(inc[1], units[1]).getValue("arcsec"));
    _output.define("pixelsperarcsec", pixelsPerArcsec);
    if (_doZeroLevel) {
        Record z;
        z.define("value", Vector<Double>(_zeroLevelOffsetSolution));
        z.define("unit", _bUnit);
        _output.defineRecord("zerooff", z);
        z.define("value", Vector<Double>(_zeroLevelOffsetError));
        _output.defineRecord("zeroofferr", z);
    }
}

template <class T>
void ImageFitter2<T>::_fitLoop(Bool& anyConverged, casa::ComponentList& convolvedList, casa::ComponentList& deconvolvedList,
    SPIIT templateImage, SPIIT residualImage, SPIIT modelImage, String& resultsString) {
    Bool converged = false;
    Bool deconvolve = false;
    Bool fit = true;
    Double zeroLevelOffsetSolution, zeroLevelOffsetError;
    Double zeroLevelOffsetEstimate = _doZeroLevel ? _zeroLevelOffsetEstimate : 0;
    uInt ngauss = _estimates.nelements() > 0 ? _estimates.nelements() : 1;
    Vector<String> models(ngauss, "gaussian");
    IPosition planeShape(this->_getImage()->ndim(), 1);
    casa::ImageMetaData<T> md(this->_getImage());
    Vector<Int> dirShape = md.directionShape();
    Vector<Int> dirAxisNumbers = this->_getImage()->coordinates().directionAxesNumbers();
    planeShape[dirAxisNumbers[0]] = dirShape[0];
    planeShape[dirAxisNumbers[1]] = dirShape[1];
    if (_doZeroLevel) {
        models.resize(ngauss + 1, true);
        models[ngauss] = "level";
        _fixed.resize(ngauss + 1, true);
        _fixed[ngauss] = _zeroLevelIsFixed ? "l" : "";
    }
    String errmsg;
    LogOrigin origin(getClass(), __func__);
    std::pair<Int, Int> pixelOffsets;
    const CoordinateSystem csys = this->_getImage()->coordinates();
    Bool hasSpectralAxis = csys.hasSpectralAxis();
    uInt spectralAxisNumber = csys.spectralAxisNumber();
    Bool outputImages = residualImage || modelImage;
    std::shared_ptr<ArrayLattice<Bool>> initMask;
    std::shared_ptr<TempImage<T>> tImage;
    IPosition location(this->_getImage()->ndim(), 0);
    for (_curChan = _chanVec[0]; _curChan <= _chanVec[1]; _curChan++) {
        if (_chanPixNumber >= 0) {
            _chanPixNumber = _curChan;
        }
        Fit2D fitter(*this->_getLog());
        _setIncludeExclude(fitter);
        Array<T> pixels;
        Array<Bool> pixelMask;
        _curConvolvedList = casa::ComponentList();
        _curDeconvolvedList = casa::ComponentList();
        try {
            _fitsky(fitter, pixels, pixelMask, converged, zeroLevelOffsetSolution, zeroLevelOffsetError, pixelOffsets, models, fit,
                deconvolve, zeroLevelOffsetEstimate);
        } catch (const AipsError& x) {
            *this->_getLog() << origin << LogIO::WARN << "Fit failed to converge "
                             << "because of exception: " << x.getMesg() << LogIO::POST;
            converged = false;
        }
        *this->_getLog() << origin;
        anyConverged |= converged;
        if (converged) {
            _doConverged(convolvedList, deconvolvedList, zeroLevelOffsetEstimate, pixelOffsets, residualImage, modelImage, tImage, initMask,
                zeroLevelOffsetSolution, zeroLevelOffsetError, hasSpectralAxis, spectralAxisNumber, outputImages, planeShape, pixels,
                pixelMask, fitter);
        } else {
            if (_doZeroLevel) {
                _zeroLevelOffsetSolution.push_back(doubleNaN());
                _zeroLevelOffsetError.push_back(doubleNaN());
            }
            if (outputImages) {
                if (hasSpectralAxis) {
                    location[spectralAxisNumber] = _curChan - _chanVec[0];
                }
                Array<T> x(templateImage->shape());
                x.set(0);
                if (residualImage) {
                    residualImage->putSlice(x, location);
                }
                if (modelImage) {
                    modelImage->putSlice(x, location);
                }
            }
        }
        _fitDone = true;
        _fitConverged[_curChan - _chanVec[0]] = converged;
        if (converged) {
            Record estimatesRecord;
            _calculateErrors();
            _setDeconvolvedSizes();
            _curConvolvedList.toRecord(errmsg, estimatesRecord);
            *this->_getLog() << origin;
        }
        _results.setConvolvedList(_curConvolvedList);
        _results.setFixed(_fixed);
        _results.setFluxDensities(_fluxDensities);
        _results.setFluxDensityErrors(_fluxDensityErrors);
        _results.setMajorAxes(_majorAxes);
        _results.setMinorAxes(_minorAxes);
        _results.setPeakIntensities(_peakIntensities);
        _results.setPeakIntensityErrors(_peakIntensityErrors);
        _results.setPositionAngles(_positionAngles);
        auto currentResultsString = _resultsToString(fitter.numberPoints());
        resultsString += currentResultsString;
        *this->_getLog() << LogIO::NORMAL << currentResultsString << LogIO::POST;
    }
}

template <class T>
void ImageFitter2<T>::_doConverged(casa::ComponentList& convolvedList, casa::ComponentList& deconvolvedList,
    Double& zeroLevelOffsetEstimate, std::pair<Int, Int>& pixelOffsets, SPIIT& residualImage, SPIIT& modelImage,
    std::shared_ptr<TempImage<T>>& tImage, std::shared_ptr<ArrayLattice<Bool>>& initMask, Double zeroLevelOffsetSolution,
    Double zeroLevelOffsetError, Bool hasSpectralAxis, Int spectralAxisNumber, Bool outputImages, const IPosition& planeShape,
    const Array<T>& pixels, const Array<Bool>& pixelMask, const Fit2D& fitter) {
    convolvedList.addList(_curConvolvedList);
    deconvolvedList.addList(_curDeconvolvedList);
    String error;
    if (_doZeroLevel) {
        _zeroLevelOffsetSolution.push_back(zeroLevelOffsetSolution);
        _zeroLevelOffsetError.push_back(zeroLevelOffsetError);
        zeroLevelOffsetEstimate = zeroLevelOffsetSolution;
    }
    IPosition location(this->_getImage()->ndim(), 0);
    if (hasSpectralAxis) {
        location[spectralAxisNumber] = _curChan;
    }
    Array<T> data = outputImages ? this->_getImage()->getSlice(location, planeShape, true) : pixels;
    if (!outputImages) {
        pixelOffsets.first = 0;
        pixelOffsets.second = 0;
    }
    Array<T> curResidPixels, curModelPixels;
    fitter.residual(curResidPixels, curModelPixels, data, pixelOffsets.first, pixelOffsets.second);
    std::shared_ptr<TempImage<T>> fittedResid;
    if (modelImage) {
        modelImage->putSlice(curModelPixels, location);
    }
    if (residualImage) {
        residualImage->putSlice(curResidPixels, location);
        fittedResid = std::dynamic_pointer_cast<TempImage<T>>(
            casa::SubImageFactory<T>::createImage(*residualImage, "", *this->_getRegion(), this->_getMask(), false, false, false, false));
        ThrowIf(!fittedResid, "Dynamic cast failed");
        if (!fittedResid->hasPixelMask()) {
            fittedResid->attachMask(ArrayLattice<Bool>(Array<Bool>(fittedResid->shape(), true)));
        }
    } else {
        // coordinates arean't important, just need the stats for a masked lattice.
        tImage.reset(new TempImage<T>(curResidPixels.shape(), CoordinateUtil::defaultCoords2D()));
        initMask.reset(new ArrayLattice<Bool>(Array<Bool>(curResidPixels.shape(), true)));
        tImage->attachMask(*initMask);
        fittedResid = tImage;
        fittedResid->put(curResidPixels);
    }
    LCPixelSet lcResidMask(pixelMask, LCBox(pixelMask.shape()));
    std::unique_ptr<MaskedLattice<T>> maskedLattice(fittedResid->cloneML());
    LatticeStatistics<T> lStats(*maskedLattice, false);
    Array<Double> stat;
    lStats.getStatistic(stat, LatticeStatistics<T>::RMS, true);
    _residStats.define("rms", stat[0]);
    lStats.getStatistic(stat, LatticeStatistics<T>::SIGMA, true);
    _residStats.define("sigma", stat[0]);
    lStats.getStatistic(stat, LatticeStatistics<T>::NPTS, true);
}

template <class T>
void ImageFitter2<T>::setZeroLevelEstimate(Double estimate, Bool isFixed) {
    _doZeroLevel = true;
    _zeroLevelOffsetEstimate = estimate;
    _zeroLevelIsFixed = isFixed;
}

template <class T>
void ImageFitter2<T>::unsetZeroLevelEstimate() {
    _doZeroLevel = false;
    _zeroLevelOffsetEstimate = 0;
    _zeroLevelIsFixed = false;
}

template <class T>
void ImageFitter2<T>::getZeroLevelSolution(vector<Double>& solution, vector<Double>& error) {
    ThrowIf(!_fitDone, "Fit hasn't been done yet");
    ThrowIf(!_doZeroLevel, "Zero level was not fit");
    solution = _zeroLevelOffsetSolution;
    error = _zeroLevelOffsetError;
}

template <class T>
void ImageFitter2<T>::setRMS(const Quantity& rms) {
    Double v = rms.getValue();
    ThrowIf(v <= 0, "rms must be positive.");
    if (rms.getUnit().empty()) {
        _rms = v;
    } else {
        ThrowIf(!rms.isConform(_bUnit), "rms does not conform to units of " + _bUnit);
        _rms = rms.getValue(_bUnit);
    }
}

template <class T>
void ImageFitter2<T>::setNoiseFWHM(Double d) {
    const DirectionCoordinate dCoord = this->_getImage()->coordinates().directionCoordinate();
    _noiseFWHM.reset(new Quantity(d * _pixelWidth()));
    _correlatedNoise = d >= 1;
}

template <class T>
Quantity ImageFitter2<T>::_pixelWidth() {
    if (_pixWidth.getValue() == 0) {
        const DirectionCoordinate dCoord = this->_getImage()->coordinates().directionCoordinate();
        _pixWidth = Quantity(abs(dCoord.increment()[0]), dCoord.worldAxisUnits()[0]);
    }
    return _pixWidth;
}

template <class T>
void ImageFitter2<T>::clearNoiseFWHM() {
    _noiseFWHM.reset();
    _correlatedNoise = this->_getImage()->imageInfo().hasBeam();
    if (!_correlatedNoise) {
        *this->_getLog() << LogOrigin(getClass(), __func__) << LogIO::WARN << "noiseFWHM not specified and image has no beam, "
                         << "using uncorrelated noise expressions to calculate uncertainties" << LogIO::POST;
    }
}

template <class T>
void ImageFitter2<T>::setNoiseFWHM(const Quantity& q) {
    ThrowIf(!q.isConform("rad"), "noiseFWHM unit is not an angular unit");
    _noiseFWHM.reset(new Quantity(q));
    _correlatedNoise = q >= _pixelWidth();
    if (!_correlatedNoise) {
        *this->_getLog() << LogOrigin(getClass(), __func__) << LogIO::WARN << "noiseFWHM is less than a pixel width, "
                         << "using uncorrelated noise expressions to calculate uncertainties" << LogIO::POST;
    }
}

template <class T>
Double ImageFitter2<T>::_correlatedOverallSNR(uInt comp, Double a, Double b, Double signalToNoise) const {
    Double fac = signalToNoise / 2 * (sqrt(_majorAxes[comp] * _minorAxes[comp]) / (*_noiseFWHM)).getValue("");
    Double p = (*_noiseFWHM / _majorAxes[comp]).getValue("");
    Double fac1 = pow(1 + p * p, a / 2);
    Double q = (*_noiseFWHM / _minorAxes[comp]).getValue("");
    Double fac2 = pow(1 + q * q, b / 2);
    return fac * fac1 * fac2;
}

template <class T>
GaussianBeam ImageFitter2<T>::_getCurrentBeam() const {
    return this->_getImage()->imageInfo().restoringBeam(_chanPixNumber, _stokesPixNumber);
}

template <class T>
void ImageFitter2<T>::_calculateErrors() {
    static const Double f1 = sqrt(8 * C::ln2);
    static const Quantity fac(sqrt(C::pi) / f1, "");
    uInt ncomps = _curConvolvedList.nelements();
    _majorAxes.resize(ncomps);
    _majorAxisErrors.resize(ncomps);
    _minorAxes.resize(ncomps);
    _minorAxisErrors.resize(ncomps);
    _positionAngles.resize(ncomps);
    _positionAngleErrors.resize(ncomps);
    _fluxDensities.resize(ncomps);
    _fluxDensityErrors.resize(ncomps);
    _peakIntensities.resize(ncomps);
    _peakIntensityErrors.resize(ncomps);
    auto rms = _getRMS();
    auto pixelWidth = _pixelWidth();

    casa::PeakIntensityFluxDensityConverter<T> converter(this->_getImage());
    converter.setVerbosity(casa::ImageTask<T>::NORMAL);
    converter.setShape(casa::ComponentType::GAUSSIAN);
    converter.setBeam(_chanPixNumber, _stokesPixNumber);
    if (_useBeamForNoise) {
        auto beam = _getCurrentBeam();
        _noiseFWHM.reset(new Quantity(sqrt(beam.getMajor() * beam.getMinor()).get("arcsec")));
    }
    Double signalToNoise = 0;
    for (uInt i = 0; i < ncomps; ++i) {
        const auto* gShape = static_cast<const casa::GaussianShape*>(_curConvolvedList.getShape(i));
        _majorAxes[i] = gShape->majorAxis();
        _minorAxes[i] = gShape->minorAxis();
        _positionAngles[i] = gShape->positionAngle();
        Vector<Quantity> fluxQuant;
        _curConvolvedList.getFlux(fluxQuant, i);
        // TODO there is probably a better way to get the flux component we want...
        auto polarization = _curConvolvedList.getStokes(i);
        uInt polnum = 0;
        for (uInt j = 0; j < polarization.size(); ++j) {
            if (polarization[j] == _kludgedStokes) {
                _fluxDensities[i] = fluxQuant[j];
                polnum = j;
                break;
            }
        }
        Double baseFac = 0;
        {
            // peak intensities and peak intensity errors

            converter.setSize(Angular2DGaussian(_majorAxes[i], _minorAxes[i], Quantity(0, "deg")));
            _peakIntensities[i] = converter.fluxDensityToPeakIntensity(_noBeam, _fluxDensities[i]);
            // peak is already in brightness unit which is what we want
            signalToNoise = abs(_peakIntensities[i]).getValue() / rms;
            if (_correlatedNoise) {
                Double overallSNR = _correlatedOverallSNR(i, 1.5, 1.5, signalToNoise);
                baseFac = C::sqrt2 / overallSNR;
            } else {
                // same baseFac used for all uncorrelated noise parameter
                // error calculations
                Quantity fac2 = fac / pixelWidth;
                auto overallSNR = (fac2 * signalToNoise * sqrt(_majorAxes[i] * _minorAxes[i])).getValue("");
                baseFac = C::sqrt2 / overallSNR;
            }
            _peakIntensityErrors[i] =
                _fixed[i].contains("f") ? Quantity(0, _peakIntensities[i].getUnit()) : baseFac * abs(_peakIntensities[i]);
            _allConvolvedPeakIntensities.push_back(_peakIntensities[i]);
            _allConvolvedPeakIntensityErrors.push_back(_peakIntensityErrors[i]);
        }
        Double cor1 = !_correlatedNoise || (_fixed[i].contains("a") && _fixed[i].contains("x"))
                          ? 0
                          : C::sqrt2 / _correlatedOverallSNR(i, 2.5, 0.5, signalToNoise);
        // the only way for the minor axis to be fixed is if both the major axis
        // and the axial ratio (b) are fixed
        Double cor2 =
            !_correlatedNoise || (_fixed[i].contains("a") && _fixed[i].contains("b") && _fixed[i].contains("y") && _fixed[i].contains("p"))
                ? 0
                : C::sqrt2 / _correlatedOverallSNR(i, 0.5, 2.5, signalToNoise);
        std::unique_ptr<casa::GaussianShape> newShape(dynamic_cast<casa::GaussianShape*>(gShape->clone()));
        {
            // major and minor axes and position angle errors
            if (_fixed[i].contains("a")) {
                _majorAxisErrors[i] = Quantity(0, _majorAxes[i].getUnit());
            } else {
                if (_correlatedNoise) {
                    baseFac = cor1;
                }
                _majorAxisErrors[i] = baseFac * _majorAxes[i];
            }
            // b means keep the *axial ratio* fixed
            if (_fixed[i].contains("b") && _fixed[i].contains("a")) {
                // both major and minor axes fixed
                _minorAxisErrors[i] = Quantity(0, _minorAxes[i].getUnit());
            } else {
                if (_correlatedNoise) {
                    baseFac = cor2;
                }
                _minorAxisErrors[i] = baseFac * _minorAxes[i];
            }
            if (_fixed[i].contains("p")) {
                _positionAngleErrors[i] = Quantity(0, "rad");
            } else {
                if (_correlatedNoise) {
                    baseFac = cor2;
                }
                Double ma = _majorAxes[i].getValue("arcsec");
                Double mi = _minorAxes[i].getValue("arcsec");
                _positionAngleErrors[i] = ma == mi ? QC::qTurn() : Quantity(baseFac * C::sqrt2 * (ma * mi / (ma * ma - mi * mi)), "rad");
            }
            _positionAngleErrors[i].convert(_positionAngles[i]);
            newShape->setErrors(_majorAxisErrors[i], _minorAxisErrors[i], _positionAngleErrors[i]);
        }
        {
            // x and y errors
            Bool fixFullPos = _fixed[i].contains("x") && _fixed[i].contains("y");
            Quantity sigmaX0, sigmaY0;
            if (fixFullPos) {
                sigmaX0 = Quantity(0, "arcsec");
                sigmaY0 = Quantity(0, "arcsec");
            } else {
                if (_correlatedNoise) {
                    baseFac = cor1;
                }
                sigmaX0 = baseFac * _majorAxes[i] / f1;
                if (_correlatedNoise) {
                    baseFac = cor2;
                }
                sigmaY0 = baseFac * _minorAxes[i] / f1;
            }
            Double pr = fixFullPos ? 0 : (-1) * (_positionAngles[i] + QC::qTurn()).getValue("rad");
            Double cp = fixFullPos ? 0 : cos(pr);
            Double sp = fixFullPos ? 0 : sin(pr);
            Quantity longErr(0, "arcsec");
            if (!_fixed[i].contains("x")) {
                Double xc = (sigmaX0 * cp).getValue("arcsec");
                Double ys = (sigmaY0 * sp).getValue("arcsec");
                longErr.setValue(sqrt(xc * xc + ys * ys));
            }
            Quantity latErr(0, "arcsec");
            if (!_fixed[i].contains("y")) {
                Double xs = (sigmaX0 * sp).getValue("arcsec");
                Double yc = (sigmaY0 * cp).getValue("arcsec");
                latErr.setValue(sqrt(xs * xs + yc * yc));
            }
            newShape->setRefDirectionError(latErr, longErr);
        }
        {
            // flux density errors
            if (_correlatedNoise) {
                Double fracA = (_peakIntensityErrors[i] / _peakIntensities[i]).getValue("");
                Double fracMaj = (_majorAxisErrors[i] / _majorAxes[i]).getValue("");
                Double fracMin = (_minorAxisErrors[i] / _minorAxes[i]).getValue("");
                Double y = (*_noiseFWHM * (*_noiseFWHM) / _majorAxes[i] / _minorAxes[i]).getValue("");
                _fluxDensityErrors[i] = _fluxDensities[i];
                _fluxDensityErrors[i] *= sqrt(fracA * fracA + y * (fracMaj * fracMaj + fracMin * fracMin));
            } else {
                _fluxDensityErrors[i] =
                    _fixed[i].contains("f") && _fixed[i].contains("a") && _fixed[i].contains("b") ? 0 : baseFac * _fluxDensities[i];
            }
        }
        _allFluxDensities.push_back(_fluxDensities[i]);
        _allFluxDensityErrors.push_back(_fluxDensityErrors[i]);
        _curConvolvedList.setShape(Vector<Int>(1, i), *newShape);
        Vector<std::complex<double>> errors(4, std::complex<double>(0, 0));
        errors[polnum] = std::complex<double>(_fluxDensityErrors[i].getValue(), 0);
        _curConvolvedList.component(i).flux().setErrors(errors);
        _curConvolvedList.component(i).flux().setErrors(errors);
        if (this->_getImage()->imageInfo().hasBeam()) {
            _curDeconvolvedList.component(i).flux().setErrors(errors);
        }
    }
}

template <class T>
void ImageFitter2<T>::_setIncludeExclude(Fit2D& fitter) const {
    *this->_getLog() << LogOrigin(_class, __func__);
    ThrowIf(_includePixelRange && _excludePixelRange, "You cannot give both an include and an exclude pixel range");
    if (!_includePixelRange && !_excludePixelRange) {
        return;
    } else if (_includePixelRange) {
        auto* first = &_includePixelRange->first;
        auto* second = &_includePixelRange->second;
        if (casacore::near(*first, *second)) {
            *first = -abs(*first);
            *second = -(*first);
        }
        fitter.setIncludeRange(*first, *second);
        *this->_getLog() << LogIO::NORMAL << "Selecting pixels from " << *first << " to " << *second << LogIO::POST;
    } else {
        auto* first = &_excludePixelRange->first;
        auto* second = &_excludePixelRange->second;
        if (casacore::near(*first, *second)) {
            *first = -abs(*first);
            *second = -(*first);
        }
        fitter.setExcludeRange(*first, *second);
        *this->_getLog() << LogIO::NORMAL << "Excluding pixels from " << *first << " to " << *second << LogIO::POST;
    }
}

template <class T>
Bool ImageFitter2<T>::converged(uInt plane) const {
    ThrowIf(!_fitDone, "fit has not yet been performed");
    return _fitConverged[plane];
}

template <class T>
Vector<Bool> ImageFitter2<T>::converged() const {
    return _fitConverged;
}

template <class T>
void ImageFitter2<T>::_getStandardDeviations(Double& inputStdDev, Double& residStdDev) const {
    inputStdDev = _getStatistic("sigma", _curChan - _chanVec[0], inputStats);
    residStdDev = _getStatistic("sigma", 0, _residStats);
}

template <class T>
void ImageFitter2<T>::_getRMSs(Double& inputRMS, Double& residRMS) const {
    inputRMS = _getStatistic("rms", _curChan - _chanVec[0], inputStats);
    residRMS = _getStatistic("rms", 0, _residStats);
}

template <class T>
Double ImageFitter2<T>::_getStatistic(const String& type, const uInt index, const Record& stats) const {
    Vector<Double> statVec;
    stats.get(stats.fieldNumber(type), statVec);
    return statVec[index];
}

template <class T>
vector<casa::OutputDestinationChecker::OutputStruct> ImageFitter2<T>::_getOutputStruct() {
    casa::OutputDestinationChecker::OutputStruct newEstFile;
    newEstFile.label = "new estimates file";
    newEstFile.outputFile = &_newEstimatesFileName;
    newEstFile.required = false;
    newEstFile.replaceable = true;
    vector<casa::OutputDestinationChecker::OutputStruct> outputs(1);
    outputs[0] = newEstFile;
    return outputs;
}

template <class T>
vector<Coordinate::Type> ImageFitter2<T>::_getNecessaryCoordinates() const {
    vector<Coordinate::Type> coordType(1);
    coordType[0] = Coordinate::DIRECTION;
    return coordType;
}

template <class T>
casa::CasacRegionManager::StokesControl ImageFitter2<T>::_getStokesControl() const {
    return casa::CasacRegionManager::USE_FIRST_STOKES;
}

template <class T>
void ImageFitter2<T>::_finishConstruction(const String& estimatesFilename) {
    *this->_getLog() << LogOrigin(_class, __func__);
    //_setSupportsLogfile(true);
    // <todo> kludge because Flux class is really only made for I, Q, U, and V stokes

    _stokesPixNumber = this->_getImage()->coordinates().hasPolarizationCoordinate()
                           ? this->_getImage()->coordinates().stokesPixelNumber(this->_getStokes())
                           : -1;

    String iquv = "IQUV";
    _kludgedStokes = (iquv.index(this->_getStokes()) == String::npos) || this->_getStokes().empty() ? "I" : String(this->_getStokes());
    // </todo>
    if (estimatesFilename.empty()) {
        _fixed.resize(1);
        *this->_getLog() << LogIO::NORMAL << "No estimates file specified, so will attempt to find and fit one gaussian." << LogIO::POST;
    } else {
        casa::FitterEstimatesFileParser parser(estimatesFilename, *this->_getImage());
        _estimates = parser.getEstimates();
        _estimatesString = parser.getContents();
        _fixed = parser.getFixed();
        *this->_getLog() << LogIO::NORMAL << "File " << estimatesFilename << " has " << _estimates.nelements()
                         << " specified, so will attempt to fit that many gaussians " << LogIO::POST;
    }

    casa::CasacRegionManager rm(this->_getImage()->coordinates());
    uInt nSelectedChannels;
    _chanVec =
        Vector<uInt>(this->_getChans().empty() ? rm.setSpectralRanges(nSelectedChannels, this->_getRegion(), this->_getImage()->shape())
                                               : rm.setSpectralRanges(this->_getChans(), nSelectedChannels, this->_getImage()->shape()));
    if (_chanVec.size() == 0) {
        _chanVec.resize(2);
        _chanVec.set(0);
        nSelectedChannels = 1;
        _chanPixNumber = -1;
    } else if (_chanVec.size() > 2) {
        *this->_getLog() << "Only a single contiguous channel range is supported" << LogIO::EXCEPTION;
    } else {
        _chanPixNumber = _chanVec[0];
    }
    _fitConverged.resize(nSelectedChannels);
    // check units
    Quantity q = Quantity(1, _bUnit);
    Bool unitOK = q.isConform("Jy/rad2") || q.isConform("Jy*m/s/rad2") || q.isConform("K");
    if (!unitOK) {
        Vector<String> angUnits(2, "beam");
        angUnits[1] = "pixel";
        for (uInt i = 0; i < angUnits.size(); i++) {
            if (_bUnit.contains(angUnits[i])) {
                UnitMap::putUser(angUnits[i], UnitVal(1, String("rad2")));
                if (Quantity(1, _bUnit).isConform("Jy/rad2") || Quantity(1, _bUnit).isConform("Jy*m/s/rad2") ||
                    Quantity(1, _bUnit).isConform("K")) {
                    unitOK = true;
                }
                UnitMap::removeUser(angUnits[i]);
                UnitMap::clearCache();
                if (unitOK) {
                    break;
                }
            }
        }
        if (!unitOK) {
            *this->_getLog() << LogIO::WARN << "Unrecognized intensity unit " << _bUnit << ". Will assume Jy/pixel" << LogIO::POST;
            _bUnit = "Jy/pixel";
        }
    }
}

template <class T>
String ImageFitter2<T>::_resultsToString(uInt nPixels) {
    ostringstream summary;
    summary << "*** Details of fit for channel number " << _curChan << endl;
    summary << "Number of pixels used in fit: " << nPixels << endl;
    uInt relChan = _curChan - _chanVec[0];
    if (_fitConverged[relChan]) {
        if (_noBeam) {
            *this->_getLog() << LogIO::WARN << "Flux density not reported because "
                             << "there is no clean beam in image header so these quantities cannot "
                             << "be calculated" << LogIO::POST;
        }
        summary << _statisticsToString() << endl;
        if (_doZeroLevel) {
            String units = this->_getImage()->units().getName();
            if (units.empty()) {
                units = "Jy/pixel";
            }
            summary << "Zero level offset fit: " << _zeroLevelOffsetSolution[relChan] << " +/- " << _zeroLevelOffsetError[relChan] << " "
                    << units << endl;
        }
        uInt n = _curConvolvedList.nelements();
        for (uInt i = 0; i < n; ++i) {
            shared_ptr<Vector<Double>> x;

            summary << "Fit on " << this->_getImage()->name(true) << " component " << i << endl;
            summary << _curConvolvedList.component(i).positionToString(x, &(this->_getImage()->coordinates().directionCoordinate()), true)
                    << endl;
            _pixelCoords.push_back(x);
            summary << _sizeToString(i) << endl;
            summary << _results.fluxToString(i, !_noBeam) << endl;
            summary << _spectrumToString(i) << endl;
        }
    } else {
        summary << "*** FIT FAILED ***" << endl;
    }
    return summary.str();
}

template <class T>
String ImageFitter2<T>::_statisticsToString() const {
    ostringstream stats;
    // TODO It is not clear how this chi squared value is calculated and atm it does not
    // appear to be useful, so don't report it. In the future, investigate more deeply
    // how it is calculated and see if a useful value for reporting can be derived from
    // it.
    // stats << "       --- Chi-squared of fit " << chiSquared << endl;
    stats << "Input and residual image statistics (to be used as a rough guide only as to goodness of fit)" << endl;
    Double inputStdDev, residStdDev, inputRMS, residRMS;
    _getStandardDeviations(inputStdDev, residStdDev);
    _getRMSs(inputRMS, residRMS);
    String unit = _fluxDensities[0].getUnit();
    stats << "       --- Standard deviation of input image: " << inputStdDev << " " << unit << endl;
    stats << "       --- Standard deviation of residual image: " << residStdDev << " " << unit << endl;
    stats << "       --- RMS of input image: " << inputRMS << " " << unit << endl;
    stats << "       --- RMS of residual image: " << residRMS << " " << unit << endl;
    return stats.str();
}

template <class T>
Double ImageFitter2<T>::_getRMS() const {
    if (_rms > 0) {
        return _rms;
    } else {
        return Vector<Double>(_residStats.asArrayDouble("rms"))[0];
    }
}

template <class T>
void ImageFitter2<T>::_setDeconvolvedSizes() {
    if (!this->_getImage()->imageInfo().hasBeam()) {
        return;
    }
    GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(_chanPixNumber, _stokesPixNumber);
    uInt n = _curConvolvedList.nelements();
    static const Quantity tiny(1e-60, "arcsec");
    static const Quantity zero(0, "deg");
    _deconvolvedMessages.resize(n);
    _deconvolvedMessages.set("");
    for (uInt i = 0; i < n; i++) {
        Quantity maj = _majorAxes[i];
        Quantity minor = _minorAxes[i];
        Quantity pa = _positionAngles[i];
        std::shared_ptr<casa::GaussianShape> gaussShape(static_cast<casa::GaussianShape*>(_curConvolvedList.getShape(i)->clone()));
        std::shared_ptr<casa::PointShape> point;
        Quantity emaj = _majorAxisErrors[i];
        Quantity emin = _minorAxisErrors[i];
        Quantity epa = _positionAngleErrors[i];
        Bool fitSuccess = false;
        Angular2DGaussian bestSol(maj, minor, pa);
        Angular2DGaussian bestDecon;
        Bool isPointSource = true;
        try {
            isPointSource = casa::GaussianDeconvolver::deconvolve(bestDecon, bestSol, beam);
            fitSuccess = true;
        } catch (const AipsError& x) {
            fitSuccess = false;
            isPointSource = true;
        }
        _isPoint.push_back(isPointSource);
        ostringstream size;
        Angular2DGaussian decon;
        if (fitSuccess) {
            if (isPointSource) {
                size << "    Component is a point source" << endl;
                gaussShape->setWidth(tiny, tiny, zero);
                Angular2DGaussian largest(maj + emaj, minor + emin, pa - epa);
                Bool isPointSource1 = true;
                // Bool fitSuccess1 = false;
                try {
                    isPointSource1 = casa::GaussianDeconvolver::deconvolve(decon, largest, beam);
                    fitSuccess = true;
                } catch (const AipsError& x) {
                    // fitSuccess1 = false;
                    isPointSource1 = true;
                }
                // note that the code is purposefully written in such a way that
                // fitSuccess* = false => isPointSource* = true and the conditionals
                // following rely on that fact to make the code a bit clearer
                Angular2DGaussian lsize;
                if (!isPointSource1) {
                    lsize = decon;
                }
                largest.setPA(pa + epa);
                Bool isPointSource2 = true;
                // Bool fitSuccess2 = false;
                try {
                    isPointSource2 = casa::GaussianDeconvolver::deconvolve(decon, largest, beam);
                    // fitSuccess2 = true;
                } catch (const AipsError& x) {
                    // fitSuccess2 = false;
                    isPointSource2 = true;
                }
                if (isPointSource2) {
                    if (isPointSource1) {
                        size << "    An upper limit on its size cannot be determined" << endl;
                        point.reset(new casa::PointShape());
                        point->copyDirectionInfo(*gaussShape);
                    } else {
                        size << "    It may be as large as " << std::setprecision(2) << lsize.getMajor() << " x " << lsize.getMinor()
                             << endl;
                        gaussShape->setErrors(lsize.getMajor(), lsize.getMinor(), zero);
                    }
                } else {
                    if (isPointSource1) {
                        size << "    It may be as large as " << std::setprecision(2) << decon.getMajor() << " x " << decon.getMinor()
                             << endl;
                        gaussShape->setErrors(decon.getMajor(), decon.getMinor(), zero);
                    } else {
                        Quantity lmaj = max(decon.getMajor(), lsize.getMajor());
                        Quantity lmin = max(decon.getMinor(), lsize.getMinor());
                        size << "    It may be as large as " << std::setprecision(2) << lmaj << " x " << lmin << endl;
                        gaussShape->setErrors(lmaj, lmin, zero);
                    }
                }
            } else {
                Vector<Quantity> majRange(2, maj - emaj);
                majRange[1] = maj + emaj;
                Vector<Quantity> minRange(2, minor - emin);
                minRange[1] = minor + emin;
                Vector<Quantity> paRange(2, pa - epa);
                paRange[1] = pa + epa;
                Angular2DGaussian sourceIn;
                Quantity mymajor, myminor;
                for (uInt i = 0; i < 2; i++) {
                    for (uInt j = 0; j < 2; j++) {
                        // have to check in case ranges overlap, CAS-5211
                        mymajor = max(majRange[i], minRange[j]);
                        myminor = min(majRange[i], minRange[j]);
                        if (mymajor.getValue() > 0 && myminor.getValue() > 0) {
                            sourceIn.setMajorMinor(mymajor, myminor);
                            for (uInt k = 0; k < 2; k++) {
                                sourceIn.setPA(paRange[k]);
                                decon = Angular2DGaussian();
                                Bool isPoint;
                                try {
                                    isPoint = casa::GaussianDeconvolver::deconvolve(decon, sourceIn, beam);
                                } catch (const AipsError& x) {
                                    isPoint = true;
                                }
                                if (!isPoint) {
                                    Quantity errMaj = abs(bestDecon.getMajor() - decon.getMajor());
                                    errMaj.convert(emaj.getUnit());
                                    Quantity errMin = abs(bestDecon.getMinor() - decon.getMinor());
                                    errMin.convert(emin.getUnit());
                                    Quantity errPA = abs(bestDecon.getPA(true) - decon.getPA(true));
                                    errPA = min(errPA, abs(errPA - QC::hTurn()));
                                    errPA.convert(epa.getUnit());
                                    emaj = max(emaj, errMaj);
                                    emin = max(emin, errMin);
                                    epa = max(epa, errPA);
                                }
                            }
                        }
                    }
                }
                size << casa::TwoSidedShape::sizeToString(
                    bestDecon.getMajor(), bestDecon.getMinor(), bestDecon.getPA(false), true, emaj, emin, epa);
                gaussShape->setWidth(bestDecon.getMajor(), bestDecon.getMinor(), bestDecon.getPA(false));
                gaussShape->setErrors(emaj, emin, epa);
            }
        } else {
            point.reset(new casa::PointShape());
            point->copyDirectionInfo(*gaussShape);
            size << "    Could not deconvolve source from beam. "
                 << "Source may be (only marginally) resolved in only one direction.";
        }
        _deconvolvedMessages[i] = size.str();
        if (point) {
            _curDeconvolvedList.setShape(Vector<Int>(1, i), *point);
        } else {
            _curDeconvolvedList.setShape(Vector<Int>(1, i), *gaussShape);
        }
    }
}

template <class T>
String ImageFitter2<T>::_sizeToString(const uInt compNumber) const {
    ostringstream size;
    const casa::ComponentShape* compShape = _curConvolvedList.getShape(compNumber);
    AlwaysAssert(compShape->type() == casa::ComponentType::GAUSSIAN, AipsError);
    Bool hasBeam = this->_getImage()->imageInfo().hasBeam();
    size << "Image component size";
    if (hasBeam) {
        size << " (convolved with beam)";
    }
    size << " ---" << endl;
    if (_fixed[compNumber].contains("b") && !_fixed[compNumber].contains("a")) {
        size << "       AXIAL RATIO WAS HELD FIXED DURING THE FIT" << endl;
    }
    size << compShape->sizeToString() << endl;
    if (hasBeam) {
        GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(_chanPixNumber, _stokesPixNumber);
        size << "Clean beam size ---" << endl;
        // CAS-4577, users want two digits, so just do it explicitly here rather than using
        // TwoSidedShape::sizeToString
        size << std::fixed << std::setprecision(2) << "       --- major axis FWHM: " << beam.getMajor() << endl;
        size << "       --- minor axis FWHM: " << beam.getMinor() << endl;
        size << "       --- position angle: " << beam.getPA(true) << endl;
        size << "Image component size (deconvolved from beam) ---" << endl;
        size << _deconvolvedMessages[compNumber];
    }
    return size.str();
}

template <class T>
String ImageFitter2<T>::_spectrumToString(uInt compNumber) const {
    const auto unitPrefix = casa::ImageFitterResults<T>::unitPrefixes(true);
    ostringstream spec;
    const casa::SpectralModel& spectrum = _curConvolvedList.component(compNumber).spectrum();
    auto frequency = spectrum.refFrequency().get("MHz");
    Quantity c(C::c, "m/s");
    auto wavelength = c / frequency;
    String prefUnit;
    for (uInt i = 0; i < unitPrefix.size(); i++) {
        prefUnit = unitPrefix[i] + "Hz";
        if (frequency.getValue(prefUnit) > 1) {
            frequency.convert(prefUnit);
            break;
        }
    }
    for (uInt i = 0; i < unitPrefix.size(); i++) {
        prefUnit = unitPrefix[i] + "m";
        if (wavelength.getValue(prefUnit) > 1) {
            wavelength.convert(prefUnit);
            break;
        }
    }

    spec << "Spectrum ---" << endl;
    spec << std::setprecision(7) << std::showpoint << "      --- frequency:        " << frequency << " (" << wavelength << ")" << endl;
    return spec.str();
}

template <class T>
SPIIT ImageFitter2<T>::_createImageTemplate() const {
    auto x = casa::SubImageFactory<T>::createImage(*this->_getImage(), "", Record(), "", false, false, false, false);
    x->set(0.0);
    return x;
}

template <class T>
void ImageFitter2<T>::_fitsky(Fit2D& fitter, Array<T>& pixels, Array<Bool>& pixelMask, Bool& converged, Double& zeroLevelOffsetSolution,
    Double& zeroLevelOffsetError, std::pair<Int, Int>& pixelOffsets, const Vector<String>& models, const Bool fitIt,
    const Bool deconvolveIt, const Double zeroLevelEstimate) {
    LogOrigin origin(_class, __func__);
    *this->_getLog() << origin;
    String error;
    Vector<casa::SkyComponent> estimate;
    uInt n = _estimates.nelements();
    estimate.resize(n);
    for (uInt i = 0; i < n; i++) {
        estimate(i) = _estimates.component(i);
    }
    converged = false;
    const uInt nModels = models.nelements();
    const uInt nGauss = _doZeroLevel ? nModels - 1 : nModels;
    const uInt nMasks = _fixed.nelements();
    const uInt nEstimates = estimate.nelements();
    ThrowIf(nModels == 0, "No models have been specified");
    ThrowIf(nGauss > 1 && estimate.nelements() < nGauss, "An estimate must be specified for each model component");
    ThrowIf(!fitIt && nModels > 1, "Parameter estimates are only available for a single Gaussian model");
    auto subImageTmp = casa::SubImageFactory<T>::createImage(
        *this->_getImage(), "", *this->_getRegion(), this->_getMask(), false, false, false, this->_getStretch());
    casa::ImageMetaData<T> md(subImageTmp);
    ThrowIf(anyTrue(md.directionShape() <= 1),
        "Invalid region specification. The extent of the region in the direction plane must be "
        "at least two pixels in both dimensions");
    Vector<Double> imRefPix = this->_getImage()->coordinates().directionCoordinate().referencePixel();
    Vector<Double> subRefPix = subImageTmp->coordinates().directionCoordinate().referencePixel();
    pixelOffsets.first = (int)floor(subRefPix[0] - imRefPix[0] + 0.5);
    pixelOffsets.second = (int)floor(subRefPix[1] - imRefPix[1] + 0.5);
    SubImage<T> allAxesSubImage;
    {
        IPosition imShape = subImageTmp->shape();
        IPosition startPos(imShape.nelements(), 0);
        // Pass in an IPosition here to the constructor
        // this will subtract 1 from each element of the IPosition imShape
        IPosition endPos(imShape - 1);
        IPosition stride(imShape.nelements(), 1);
        const CoordinateSystem& imcsys = subImageTmp->coordinates();
        if (imcsys.hasSpectralAxis()) {
            uInt spectralAxisNumber = imcsys.spectralAxisNumber();
            startPos[spectralAxisNumber] = _curChan - _chanVec[0];
            endPos[spectralAxisNumber] = startPos[spectralAxisNumber];
        }
        if (imcsys.hasPolarizationCoordinate()) {
            uInt stokesAxisNumber = imcsys.polarizationAxisNumber();
            startPos[stokesAxisNumber] = imcsys.stokesPixelNumber(this->_getStokes());
            endPos[stokesAxisNumber] = startPos[stokesAxisNumber];
        }
        Slicer slice(startPos, endPos, stride, Slicer::endIsLast);
        // CAS-1966, CAS-2633 keep degenerate axes
        allAxesSubImage = SubImage<T>(*subImageTmp, slice, false, AxesSpecifier(true));
    }
    // for some things we don't want the degenerate axes,
    // so make a subimage without them as well
    SubImage<T> subImage = SubImage<T>(allAxesSubImage, AxesSpecifier(false));
    // Make sure the region is 2D and that it holds the sky.  Exception if not.
    const CoordinateSystem& cSys = subImage.coordinates();
    Bool xIsLong = cSys.isDirectionAbscissaLongitude();
    pixels = subImage.get(true);
    pixelMask = subImage.getMask(true).copy();
    // What Stokes type does this plane hold ?
    Stokes::StokesTypes stokes = Stokes::type(_kludgedStokes);
    // Form masked array and find min/max
    MaskedArray<T> maskedPixels(pixels, pixelMask, true);
    T minVal, maxVal;
    IPosition minPos(2), maxPos(2);
    minMax(minVal, maxVal, minPos, maxPos, pixels);
    // Recover just single component estimate if desired and bug out
    // Must use subImage in calls as converting positions to absolute
    // pixel and vice versa
    if (!fitIt) {
        Vector<Double> parameters;
        parameters = _singleParameterEstimate(fitter, Fit2D::GAUSSIAN, maskedPixels, minVal, maxVal, minPos, maxPos);

        // Encode as SkyComponent and return
        Vector<casa::SkyComponent> result(1);
        Double facToJy;
        result(0) =
            casa::SkyComponentFactory::encodeSkyComponent(*this->_getLog(), facToJy, allAxesSubImage, _convertModelType(Fit2D::GAUSSIAN),
                parameters, stokes, xIsLong, deconvolveIt, this->_getImage()->imageInfo().restoringBeam(_chanPixNumber, _stokesPixNumber));
        _curConvolvedList.add(result(0));
    }
    // For ease of use, make each model have a mask string
    Vector<String> fixedParameters = _fixed.copy();
    fixedParameters.resize(nModels, true);
    for (uInt j = 0; j < nModels; ++j) {
        if (j >= nMasks) {
            fixedParameters(j) = String("");
        }
    }
    // Add models
    Vector<String> modelTypes = models.copy();
    ThrowIf(nEstimates == 0 && nGauss > 1, "Can only auto estimate for a gaussian model");
    for (uInt i = 0; i < nModels; ++i) {
        // If we ask to fit a POINT component, that really means a
        // Gaussian of shape the restoring beam.  So fix the shape
        // parameters and make it Gaussian
        Fit2D::Types modelType;
        if (casa::ComponentType::shape(models(i)) == casa::ComponentType::POINT) {
            modelTypes[i] = "GAUSSIAN";
            fixedParameters[i] += "abp";
        }
        modelType = Fit2D::type(modelTypes(i));
        auto parameterMask = Fit2D::convertMask(fixedParameters[i], modelType);
        Vector<Double> parameters;
        if (nEstimates == 0 && modelType == Fit2D::GAUSSIAN) {
            // Auto estimate
            parameters = _singleParameterEstimate(fitter, modelType, maskedPixels, minVal, maxVal, minPos, maxPos);
            *this->_getLog() << origin;
        } else if (modelType == Fit2D::LEVEL) {
            parameters.resize(1);
            parameters[0] = zeroLevelEstimate;
        } else {
            // Decode parameters from estimate
            const CoordinateSystem& cSys = subImage.coordinates();
            const ImageInfo& imageInfo = subImage.imageInfo();

            if (modelType == Fit2D::GAUSSIAN) {
                parameters = casa::SkyComponentFactory::decodeSkyComponent(estimate[i], imageInfo, cSys, _bUnit, stokes, xIsLong);
            }
            // The estimate SkyComponent may not be the same type as the
            // model type we are fitting for.  Try and do something about
            // this if need be by adding or removing component shape parameters
            casa::ComponentType::Shape estType = estimate(i).shape().type();
            if ((modelType == Fit2D::GAUSSIAN || modelType == Fit2D::DISK) && estType == casa::ComponentType::POINT) {
                _fitskyExtractBeam(parameters, imageInfo, xIsLong, cSys);
            }
        }
        fitter.addModel(modelType, parameters, parameterMask);
    }
    Array<T> sigma;
    Fit2D::ErrorTypes status = fitter.fit(pixels, pixelMask, sigma);
    *this->_getLog() << LogOrigin(_class, __func__);

    if (status == Fit2D::OK) {
        *this->_getLog() << LogIO::NORMAL << "Fitter was able to find a solution in " << fitter.numberIterations() << " iterations."
                         << LogIO::POST;
        converged = true;
    } else {
        converged = false;
        *this->_getLog() << LogIO::WARN << fitter.errorMessage() << LogIO::POST;
        return;
    }
    Vector<casa::SkyComponent> result(_doZeroLevel ? nModels - 1 : nModels);
    Double facToJy;
    uInt j = 0;
    Bool doDeconvolved = this->_getImage()->imageInfo().hasBeam();
    GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(_chanPixNumber, _stokesPixNumber);
    for (uInt i = 0; i < nModels; ++i) {
        if (fitter.type(i) == Fit2D::LEVEL) {
            zeroLevelOffsetSolution = fitter.availableSolution(i)[0];
            zeroLevelOffsetError = fitter.availableErrors(i)[0];
        } else {
            casa::ComponentType::Shape modelType = _convertModelType(Fit2D::type(modelTypes(i)));
            Vector<Double> solution = fitter.availableSolution(i);
            Vector<Double> errors = fitter.availableErrors(i);
            ThrowIf(anyLT(errors, 0.0), "At least one calculated error is less than zero");
            try {
                result[j] = casa::SkyComponentFactory::encodeSkyComponent(
                    *this->_getLog(), facToJy, allAxesSubImage, modelType, solution, stokes, xIsLong, deconvolveIt, beam);
            } catch (const AipsError& x) {
                ostringstream os;
                os << "Fit converged but transforming fit in pixel to world coordinates failed. "
                   << "Fit may be nonsensical, especially if any of the following fitted values "
                   << "are extremely large: " << solution << ". The lower level exception message is " << x.getMesg();
                ThrowCc(os.str());
            }
            String error;
            Record r;
            result[j].flux().toRecord(error, r);
            _curConvolvedList.add(result[j]);
            if (doDeconvolved) {
                _curDeconvolvedList.add(result[j].copy());
            }
            _setSum(result[j], subImage, j);
            _allChanNums.push_back(_curChan);
            ++j;
        }
    }
    _setBeam(beam, j);
}

template <class T>
void ImageFitter2<T>::_setBeam(GaussianBeam& beam, uInt ngauss) {
    if (beam.isNull()) {
        return;
    }
    beam.convert("arcsec", "arcsec", "deg");
    Double ster = beam.getArea("sr");
    Double _pWidth = _pixelWidth().getValue("rad");
    Double pixelArea = _pWidth * _pWidth;
    Double pixels = ster / pixelArea;
    for (uInt i = 0; i < ngauss; i++) {
        _allBeams.push_back(beam);
        _allBeamsPix.push_back(pixels);
        _allBeamsSter.push_back(ster);
    }
}

template <class T>
void ImageFitter2<T>::_setSum(const casa::SkyComponent& comp, const SubImage<T>& im, uInt compNum) {
    const casa::GaussianShape& g = static_cast<const casa::GaussianShape&>(comp.shape());
    Quantum<Vector<Double>> dir = g.refDirection().getAngle();
    Quantity xcen(dir.getValue()[0], dir.getUnit());
    Quantity ycen(dir.getValue()[1], dir.getUnit());
    Quantity sMajor = g.majorAxis() / 2;
    Quantity sMinor = g.minorAxis() / 2;
    Quantity pa = g.positionAngle();
    const Vector<Stokes::StokesTypes> stokes(0);
    casa::AnnEllipse x(xcen, ycen, sMajor, sMinor, pa, im.coordinates(), im.shape(), stokes);
    Record r = x.getRegion()->toRecord("");
    auto tmp = casa::SubImageFactory<T>::createImage(im, "", r, "", true, false, true, false);
    casa::ImageStatsCalculator<T> statsCalc(tmp, 0, String(""), false);
    statsCalc.setList(false);
    statsCalc.setVerbose(false);
    Array<Double> mySums = statsCalc.statistics().asArrayDouble("sum");
    Double mysum = 0;
    if (mySums.empty()) {
        *this->_getLog() << LogIO::WARN << "Found no pixels over which to "
                         << "sum for component " << compNum << ". This may indicate a "
                         << "nonsensical result, possibly due to over fitting. "
                         << "Proceeding anyway." << LogIO::POST;
    } else {
        mysum = *mySums.begin();
    }
    _allSums.push_back(Quantity(mysum, _bUnit));
}

template <class T>
Vector<Double> ImageFitter2<T>::_singleParameterEstimate(Fit2D& fitter, Fit2D::Types model, const MaskedArray<T>& pixels, T minVal,
    T maxVal, const IPosition& minPos, const IPosition& maxPos) const {
    // position angle +x -> +y

    // Return the initial fit guess as either the model, an auto guess,
    // or some combination.
    *this->_getLog() << LogOrigin(_class, __func__);
    Vector<Double> parameters;
    if (model == Fit2D::GAUSSIAN || model == Fit2D::DISK) {
        // Auto determine estimate
        parameters = fitter.estimate(model, pixels.getArray(), pixels.getMask());
        if (parameters.nelements() == 0) {
            // Fall back parameters
            *this->_getLog() << LogIO::WARN << "The primary initial estimate failed.  Fallback may be poor" << LogIO::POST;
            parameters.resize(6);
            IPosition shape = pixels.shape();
            if (abs(minVal) > abs(maxVal)) {
                parameters(0) = minVal;            // height
                parameters(1) = Double(minPos(0)); // x cen
                parameters(2) = Double(minPos(1)); // y cen
            } else {
                parameters(0) = maxVal;            // height
                parameters(1) = Double(maxPos(0)); // x cen
                parameters(2) = Double(maxPos(1)); // y cen
            }
            parameters(3) = Double(std::max(shape(0), shape(1)) / 2); // major axis
            parameters(4) = 0.9 * parameters(3);                      // minor axis
            parameters(5) = 0.0;                                      // position angle
        } else {
            ThrowIf(parameters.nelements() != 6, "Not enough parameters returned by fitter estimate");
        }
    } else {
        ThrowCc("Only Gaussian/Disk auto-single estimates are available");
    }
    return parameters;
}

template <class T>
casa::ComponentType::Shape ImageFitter2<T>::_convertModelType(Fit2D::Types typeIn) const {
    if (typeIn == Fit2D::GAUSSIAN) {
        return casa::ComponentType::GAUSSIAN;
    } else if (typeIn == Fit2D::DISK) {
        return casa::ComponentType::DISK;
    } else {
        throw(AipsError("Unrecognized model type"));
    }
}

template <class T>
void ImageFitter2<T>::_fitskyExtractBeam(
    Vector<Double>& parameters, const ImageInfo& imageInfo, const Bool xIsLong, const CoordinateSystem& cSys) const {
    // We need the restoring beam shape as well.
    GaussianBeam beam = imageInfo.restoringBeam(_chanPixNumber, _stokesPixNumber);
    Vector<Quantity> wParameters(5);
    // Because we convert at the reference
    // value for the beam, the position is
    // irrelevant
    wParameters(0).setValue(0.0);
    wParameters(1).setValue(0.0);
    wParameters(0).setUnit(String("rad"));
    wParameters(1).setUnit(String("rad"));
    wParameters(2) = beam.getMajor();
    wParameters(3) = beam.getMinor();
    wParameters(4) = beam.getPA();

    // Convert to pixels for Fit2D
    IPosition pixelAxes(2);
    pixelAxes(0) = 0;
    pixelAxes(1) = 1;
    if (!xIsLong) {
        pixelAxes(1) = 0;
        pixelAxes(0) = 1;
    }
    Bool doRef = true;
    Vector<Double> dParameters;
    casa::SkyComponentFactory::worldWidthsToPixel(dParameters, wParameters, cSys, pixelAxes, doRef);
    parameters.resize(6, true);
    parameters(3) = dParameters(0);
    parameters(4) = dParameters(1);
    parameters(5) = dParameters(2);
}

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_IMAGEFITTER2_TCC_
