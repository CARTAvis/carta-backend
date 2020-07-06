//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/MomentCalcBase.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_TCC_
#define CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_TCC_

#include "MomentsBase.h"

using namespace carta;

template <class T>
MomentCalcBase<T>::~MomentCalcBase() {}

template <class T>
void MomentCalcBase<T>::init(casacore::uInt nOutPixelsPerCollapse) {
    AlwaysAssert(nOutPixelsPerCollapse == 1, casacore::AipsError);
}

// Try and work out whether this spectrum is all noise
// or not.  We don't bother with it if it is noise.
// We compare the peak with sigma and a cutoff SNR
// Returns 1 if all noise
// Returns 2 if all masked
// Returns 0 otherwise
template <class T>
casacore::uInt MomentCalcBase<T>::allNoise(
    T& dMean, const casacore::Vector<T>& data, const casacore::Vector<casacore::Bool>& mask, const T peakSNR, const T stdDeviation) const {
    casacore::ClassicalStatistics<AccumType, DataIterator, MaskIterator> statsCalculator;
    statsCalculator.addData(data.begin(), mask.begin(), data.size());
    casacore::StatsData<AccumType> stats = statsCalculator.getStatistics();
    if (stats.npts == 0) {
        return 2;
    }
    T dMin = *stats.min;
    T dMax = *stats.max;
    dMean = stats.mean;

    // Assume we are continuum subtracted so outside of line mean = 0
    const T rat = max(abs(dMin), abs(dMax)) / stdDeviation;
    casacore::uInt ret = rat < peakSNR ? 1 : 0;
    return ret;
}

template <class T>
void MomentCalcBase<T>::constructorCheck(casacore::Vector<T>& calcMoments, casacore::Vector<casacore::Bool>& calcMomentsMask,
    const casacore::Vector<casacore::Int>& selectMoments, const casacore::uInt nLatticeOut) const {
    // Number of output lattices must equal the number of moments
    // the user asked to calculate
    AlwaysAssert(nLatticeOut == selectMoments.nelements(), casacore::AipsError);

    // Number of requested moments must be in allowed range
    auto nMaxMoments = MomentsBase<T>::NMOMENTS;
    AlwaysAssert(selectMoments.nelements() <= nMaxMoments, casacore::AipsError);
    AlwaysAssert(selectMoments.nelements() > 0, casacore::AipsError);

    // Resize the vector that will hold ALL possible moments
    calcMoments.resize(nMaxMoments);
    calcMomentsMask.resize(nMaxMoments);
}

template <class T>
void MomentCalcBase<T>::costlyMoments(
    MomentsBase<T>& iMom, casacore::Bool& doMedianI, casacore::Bool& doMedianV, casacore::Bool& doAbsDev) const {
    doMedianI = false;
    doMedianV = false;
    doAbsDev = false;
    using IM = MomentsBase<casacore::Float>;

    for (casacore::uInt i = 0; i < iMom.moments_p.nelements(); i++) {
        if (iMom.moments_p(i) == IM::MEDIAN) {
            doMedianI = true;
        }
        if (iMom.moments_p(i) == IM::MEDIAN_COORDINATE) {
            doMedianV = true;
        }
        if (iMom.moments_p(i) == IM::ABS_MEAN_DEVIATION) {
            doAbsDev = true;
        }
    }
}

template <class T>
casacore::Bool MomentCalcBase<T>::doFit(const MomentsBase<T>& iMom) const {
    // Get it from ImageMoments private data
    return iMom.doFit_p;
}

//
// doCoordProfile - we need the coordinate for each pixel of the profile
// doCoordRandom  - we need the coordinate for occasional use
//
// Figure out if we need to compute the coordinate of each profile pixel index
// for each profile.  This is very expensive for non-separable axes.
//
template <class T>
void MomentCalcBase<T>::doCoordCalc(casacore::Bool& doCoordProfile, casacore::Bool& doCoordRandom, const MomentsBase<T>& iMom) const {
    doCoordProfile = false;
    doCoordRandom = false;
    using IM = MomentsBase<casacore::Float>;
    for (casacore::uInt i = 0; i < iMom.moments_p.nelements(); i++) {
        if (iMom.moments_p(i) == IM::WEIGHTED_MEAN_COORDINATE || iMom.moments_p(i) == IM::WEIGHTED_DISPERSION_COORDINATE) {
            doCoordProfile = true;
        }
        if (iMom.moments_p(i) == IM::MAXIMUM_COORDINATE || iMom.moments_p(i) == IM::MINIMUM_COORDINATE ||
            iMom.moments_p(i) == IM::MEDIAN_COORDINATE) {
            doCoordRandom = true;
        }
    }
}

//
// Find the next good (or bad) point in an array.
// A good point in the array has a non-zero value.
//
// Inputs:
//  n        Number of points in array
//  mask     casacore::Vector containing counts.
//  iStart   The index of the first point to consider
//  findGood If true look for next good point.
//           If false look for next bad point
// Outputs:
//  iFound   Index of found point
//  casacore::Bool     false if didn't find another valid datum
//
template <class T>
casacore::Bool MomentCalcBase<T>::findNextDatum(casacore::uInt& iFound, const casacore::uInt& n,
    const casacore::Vector<casacore::Bool>& mask, const casacore::uInt& iStart, const casacore::Bool& findGood) const {
    for (casacore::uInt i = iStart; i < n; i++) {
        if ((findGood && mask(i)) || (!findGood && !mask(i))) {
            iFound = i;
            return true;
        }
    }
    return false;
}

//
// Fit Gaussian pos * exp(-4ln2*(x-pos)**2/width**2)
// width = fwhm
// Returns false if fit fails or all masked
// Select unmasked pixels
//
template <class T>
casacore::Bool MomentCalcBase<T>::fitGaussian(casacore::uInt& nFailed, T& peak, T& pos, T& width, T& level, const casacore::Vector<T>& x,
    const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, const T peakGuess, const T posGuess, const T widthGuess,
    const T levelGuess) const {
    casacore::uInt j = 0;
    auto nAll = y.size();
    casacore::Vector<T> xSel(nAll);
    casacore::Vector<T> ySel(nAll);
    for (casacore::uInt i = 0; i < nAll; ++i) {
        if (mask[i]) {
            xSel[j] = x[i];
            ySel[j] = y[i];
            ++j;
        }
    }
    auto nPts = j;
    if (nPts == 0) {
        return false;
    }
    xSel.resize(nPts, true);
    ySel.resize(nPts, true);

    // Create fitter as gaussian + constant offset
    casacore::NonLinearFitLM<T> fitter;
    casacore::Gaussian1D<casacore::AutoDiff<T>> gauss;
    casacore::Polynomial<casacore::AutoDiff<T>> poly;
    casacore::CompoundFunction<casacore::AutoDiff<T>> func;
    func.addFunction(gauss);
    func.addFunction(poly);
    fitter.setFunction(func);

    // Initial guess
    casacore::Vector<T> v(4);
    v[0] = peakGuess;
    v[1] = posGuess;
    v[2] = widthGuess;
    v[3] = levelGuess;
    fitter.setParameterValues(v);

    // Set maximum number of iterations to 50.  Default is 10
    fitter.setMaxIter(50);

    // Set converge criteria.
    fitter.setCriteria(0.001);

    // Perform fit on unmasked data
    casacore::Vector<T> resultSigma(nPts, 1);
    casacore::Vector<T> solution;
    try {
        solution = fitter.fit(xSel, ySel, resultSigma);
    } catch (const casacore::AipsError& x1) {
        ++nFailed;
        return false;
    }

    // Return values of fit
    // FIXME shouldn't these only be set if the fit converged?
    peak = solution[0];
    pos = solution[1];
    width = abs(solution[2]);
    level = solution[3];

    // Return status
    auto converged = fitter.converged();
    if (!converged) {
        ++nFailed;
    }
    return converged;
}

//
// Automatically fit a Gaussian and return the Gaussian parameters.
// If a plotting device is active, we also plot the spectra and fits
//
// Inputs:
//   x,y        casacore::Vector containing the data
//   mask       true is good
//   plotter    Plot spectrum and optionally the  window
//   x,yLabel   Labels
//   title
// casacore::Input/output
//   nFailed    Cumulative number of failed fits
// Output:
//   gaussPars  The gaussian parameters, peak, pos, fwhm
//   casacore::Bool       If false then this spectrum has been rejected (all
//              masked, all noise, failed fit)
//
// See if this spectrum is all noise.  If so, forget it.
// Return straight away if all masked
//
template <class T>
casacore::Bool MomentCalcBase<T>::getAutoGaussianFit(casacore::uInt& nFailed, casacore::Vector<T>& gaussPars, const casacore::Vector<T>& x,
    const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, const T peakSNR, const T stdDeviation) const {
    T dMean;
    casacore::uInt iNoise = this->allNoise(dMean, y, mask, peakSNR, stdDeviation);

    if (iNoise == 2) {
        return false;
    }
    if (iNoise == 1) {
        gaussPars = 0;
        return false;
    }

    // Work out guesses for Gaussian
    T peakGuess, posGuess, widthGuess, levelGuess;
    T pos, width, peak, level;
    if (!getAutoGaussianGuess(peakGuess, posGuess, widthGuess, levelGuess, x, y, mask)) {
        return false;
    }
    peakGuess = peakGuess - levelGuess;

    // Fit gaussian. Do it twice.
    if (!fitGaussian(nFailed, peak, pos, width, level, x, y, mask, peakGuess, posGuess, widthGuess, levelGuess)) {
        gaussPars = 0;
        return false;
    }
    gaussPars(0) = peak;
    gaussPars(1) = pos;
    gaussPars(2) = width;
    gaussPars(3) = level;

    return true;
}

template <class T>
casacore::Bool MomentCalcBase<T>::getAutoGaussianGuess(T& peakGuess, T& posGuess, T& widthGuess, T& levelGuess,
    const casacore::Vector<T>& x, const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask) const {
    // Make a wild stab in the dark as to what the Gaussian
    // parameters of this spectrum might be
    casacore::ClassicalStatistics<AccumType, DataIterator, MaskIterator> statsCalculator;
    statsCalculator.addData(y.begin(), mask.begin(), y.size());
    casacore::StatsData<AccumType> stats = statsCalculator.getStatistics();
    if (stats.npts == 0) {
        // all masked
        return false;
    }

    // Find peak and position of peak
    posGuess = x[stats.maxpos.second];
    peakGuess = *stats.max;
    levelGuess = stats.mean;

    // Nothing much is very robust.  Assume the line is reasonably
    // sampled and set its width to a few pixels.  Totally ridiculous.
    widthGuess = 5;

    return true;
}

//
// Examine an array and determine how many segments
// of good points it consists of.    A good point
// occurs if the array value is greater than zero.
//
// Inputs:
//   mask  The array mask. true is good.
// Outputs:
//   nSeg  Number of segments
//   start Indices of start of each segment
//   nPts  Number of points in segment
//
template <class T>
void MomentCalcBase<T>::lineSegments(casacore::uInt& nSeg, casacore::Vector<casacore::uInt>& start, casacore::Vector<casacore::uInt>& nPts,
    const casacore::Vector<casacore::Bool>& mask) const {
    casacore::Bool finish = false;
    nSeg = 0;
    casacore::uInt iGood, iBad;
    const casacore::uInt n = mask.nelements();
    start.resize(n);
    nPts.resize(n);

    for (casacore::uInt i = 0; !finish;) {
        if (!findNextDatum(iGood, n, mask, i, true)) {
            finish = true;
        } else {
            nSeg++;
            start(nSeg - 1) = iGood;

            if (!findNextDatum(iBad, n, mask, iGood, false)) {
                nPts(nSeg - 1) = n - start(nSeg - 1);
                finish = true;
            } else {
                nPts(nSeg - 1) = iBad - start(nSeg - 1);
                i = iBad + 1;
            }
        }
    }
    start.resize(nSeg, true);
    nPts.resize(nSeg, true);
}

template <class T>
casacore::Int& MomentCalcBase<T>::momentAxis(MomentsBase<T>& iMom) const {
    return iMom.momentAxis_p;
}

template <class T>
casacore::String MomentCalcBase<T>::momentAxisName(const casacore::CoordinateSystem& cSys, const MomentsBase<T>& iMom) const {
    // Return the name of the moment/profile axis
    casacore::Int worldMomentAxis = cSys.pixelAxisToWorldAxis(iMom.momentAxis_p);
    return cSys.worldAxisNames()(worldMomentAxis);
}

template <class T>
T& MomentCalcBase<T>::peakSNR(MomentsBase<T>& iMom) const {
    // Get it from ImageMoments private data
    return iMom.peakSNR_p;
}

template <class T>
void MomentCalcBase<T>::selectRange(
    casacore::Vector<T>& pixelRange, casacore::Bool& doInclude, casacore::Bool& doExclude, MomentsBase<T>& iMom) const {
    // Get it from ImageMoments private data
    pixelRange = iMom.selectRange_p;
    doInclude = (!(iMom.noInclude_p));
    doExclude = (!(iMom.noExclude_p));
}

//
// Fill the moment selection vector according to what the user requests
//
template <class T>
casacore::Vector<casacore::Int> MomentCalcBase<T>::selectMoments(MomentsBase<T>& iMom) const {
    using IM = MomentsBase<casacore::Float>;
    casacore::Vector<casacore::Int> sel(IM::NMOMENTS);
    casacore::uInt j = 0;

    for (casacore::uInt i = 0; i < iMom.moments_p.nelements(); i++) {
        if (iMom.moments_p(i) == IM::AVERAGE) {
            sel(j++) = IM::AVERAGE;
        } else if (iMom.moments_p(i) == IM::INTEGRATED) {
            sel(j++) = IM::INTEGRATED;
        } else if (iMom.moments_p(i) == IM::WEIGHTED_MEAN_COORDINATE) {
            sel(j++) = IM::WEIGHTED_MEAN_COORDINATE;
        } else if (iMom.moments_p(i) == IM::WEIGHTED_DISPERSION_COORDINATE) {
            sel(j++) = IM::WEIGHTED_DISPERSION_COORDINATE;
        } else if (iMom.moments_p(i) == IM::MEDIAN) {
            sel(j++) = IM::MEDIAN;
        } else if (iMom.moments_p(i) == IM::STANDARD_DEVIATION) {
            sel(j++) = IM::STANDARD_DEVIATION;
        } else if (iMom.moments_p(i) == IM::RMS) {
            sel(j++) = IM::RMS;
        } else if (iMom.moments_p(i) == IM::ABS_MEAN_DEVIATION) {
            sel(j++) = IM::ABS_MEAN_DEVIATION;
        } else if (iMom.moments_p(i) == IM::MAXIMUM) {
            sel(j++) = IM::MAXIMUM;
        } else if (iMom.moments_p(i) == IM::MAXIMUM_COORDINATE) {
            sel(j++) = IM::MAXIMUM_COORDINATE;
        } else if (iMom.moments_p(i) == IM::MINIMUM) {
            sel(j++) = IM::MINIMUM;
        } else if (iMom.moments_p(i) == IM::MINIMUM_COORDINATE) {
            sel(j++) = IM::MINIMUM_COORDINATE;
        } else if (iMom.moments_p(i) == IM::MEDIAN_COORDINATE) {
            sel(j++) = IM::MEDIAN_COORDINATE;
        }
    }
    sel.resize(j, true);

    return sel;
}

template <class T>
void MomentCalcBase<T>::setPosLabel(casacore::String& title, const casacore::IPosition& pos) const {
    ostringstream oss;
    oss << "Position = " << pos + 1;
    casacore::String temp(oss);
    title = temp;
}

template <class T>
void MomentCalcBase<T>::setCoordinateSystem(casacore::CoordinateSystem& cSys, MomentsBase<T>& iMom) {
    cSys = iMom.coordinates();
}

//
// casacore::Input:
// doCoordProfile - we need the coordinate for each pixel of the profile
//                  and we precompute it if we can
// doCoordRandom  - we need the coordinate for occasional use
//
// This function does two things.  It sets up the pixelIn
// and worldOut vectors needed by getMomentCoord. It also
// precomputes the vector of coordinates for the moment axis
// profile if it is separable
//
template <class T>
void MomentCalcBase<T>::setUpCoords(const MomentsBase<T>& iMom, casacore::Vector<casacore::Double>& pixelIn,
    casacore::Vector<casacore::Double>& worldOut, casacore::Vector<casacore::Double>& sepWorldCoord, casacore::LogIO& os,
    casacore::Double& integratedScaleFactor, const casacore::CoordinateSystem& cSys, casacore::Bool doCoordProfile,
    casacore::Bool doCoordRandom) const {
    // Do we need the scale factor for the integrated moment
    casacore::Int axis = iMom.momentAxis_p;
    casacore::Bool doIntScaleFactor = false;
    integratedScaleFactor = 1.0;
    for (casacore::uInt i = 0; i < iMom.moments_p.nelements(); i++) {
        if (iMom.moments_p(i) == MomentsBase<casacore::Float>::INTEGRATED) {
            doIntScaleFactor = true;
            break;
        }
    }

    sepWorldCoord.resize(0);
    if (!doCoordProfile && !doCoordRandom && !doIntScaleFactor) {
        return;
    }

    // Resize these vectors used for occaisional coordinate transformations
    pixelIn.resize(cSys.nPixelAxes());
    worldOut.resize(cSys.nWorldAxes());
    if (!doCoordProfile && !doIntScaleFactor) {
        return;
    }

    // Find the coordinate for the moment axis
    casacore::Int coordinate, axisInCoordinate;
    cSys.findPixelAxis(coordinate, axisInCoordinate, axis);

    // Find out whether this coordinate is separable or not
    casacore::Int nPixelAxes = cSys.coordinate(coordinate).nPixelAxes();
    casacore::Int nWorldAxes = cSys.coordinate(coordinate).nWorldAxes();

    // Precompute the profile coordinates if it is separable and needed
    // The Integrated moment scale factor is worked out here as well so the
    // logic is a bit contorted
    casacore::Bool doneIntScale = false;
    if (nPixelAxes == 1 && nWorldAxes == 1) {
        pixelIn = cSys_p.referencePixel();
        casacore::Vector<casacore::Double> frequency(iMom.getShape()(axis));
        if (doCoordProfile) {
            for (casacore::uInt i = 0; i < frequency.nelements(); i++) {
                frequency(i) = getMomentCoord(iMom, pixelIn, worldOut, casacore::Double(i));
            }
        }

        // If the coordinate of the moment axis is Spectral convert to km/s
        // Although I could work this out here, it would be decoupled from
        // ImageMoments which works the same thing out and sets the units.
        // So to ensure coupling, i pass in this switch via the IM object
        if (iMom.convertToVelocity_p) {
            AlwaysAssert(cSys.type(coordinate) == casacore::Coordinate::SPECTRAL,
                casacore::AipsError); // Should never fail !
            const casacore::SpectralCoordinate& sc = cSys.spectralCoordinate(coordinate);
            casacore::SpectralCoordinate sc0(sc);

            // Convert
            sc0.setVelocity(casacore::String("km/s"), iMom.velocityType_p);
            if (doCoordProfile) {
                sc0.frequencyToVelocity(sepWorldCoord, frequency);
            }

            // Find increment in world units at reference pixel if needed
            if (doIntScaleFactor) {
                casacore::Quantum<casacore::Double> vel0, vel1;
                casacore::Double pix0 = sc0.referencePixel()(0) - 0.5;
                casacore::Double pix1 = sc0.referencePixel()(0) + 0.5;
                sc0.pixelToVelocity(vel0, pix0);
                sc0.pixelToVelocity(vel1, pix1);
                integratedScaleFactor = abs(vel1.getValue() - vel0.getValue());
                doneIntScale = true;
            }
        }
    } else {
        os << casacore::LogIO::NORMAL << "You have asked for a coordinate moment from a non-separable " << endl;
        os << "axis.  This means a coordinate must be computed for each pixel " << endl;
        os << "of each profile which will cause performance degradation" << casacore::LogIO::POST;
    }

    if (doIntScaleFactor && !doneIntScale) {
        // We need the Integrated moment scale factor but the moment
        // axis is non-separable
        const casacore::Coordinate& c = cSys.coordinate(coordinate);
        casacore::Double inc = c.increment()(axisInCoordinate);
        integratedScaleFactor = abs(inc * inc);
        doneIntScale = true;
    }
}

template <class T>
T& MomentCalcBase<T>::stdDeviation(MomentsBase<T>& iMom) const {
    return iMom.stdDeviation_p;
}

//
// Fill the moments vector
//
// Inputs
//   integratedScaleFactor  width of a channel in km/s or Hz or whatever
// Outputs:
//   calcMoments The moments
//
template <class T>
void MomentCalcBase<T>::setCalcMoments(const MomentsBase<T>& iMom, casacore::Vector<T>& calcMoments,
    casacore::Vector<casacore::Bool>& calcMomentsMask, casacore::Vector<casacore::Double>& pixelIn,
    casacore::Vector<casacore::Double>& worldOut, casacore::Bool doCoord, casacore::Double integratedScaleFactor, T dMedian, T vMedian,
    casacore::Int nPts, typename casacore::NumericTraits<T>::PrecisionType s0, typename casacore::NumericTraits<T>::PrecisionType s1,
    typename casacore::NumericTraits<T>::PrecisionType s2, typename casacore::NumericTraits<T>::PrecisionType s0Sq,
    typename casacore::NumericTraits<T>::PrecisionType sumAbsDev, T dMin, T dMax, casacore::Int iMin, casacore::Int iMax) const {
    // casacore::Short hand to fish ImageMoments enum values out
    // Despite being our friend, we cannot refer to the
    // enum values as just, say, "AVERAGE"
    using IM = MomentsBase<casacore::Float>;

    // Normalize and fill moments
    calcMomentsMask = true;
    calcMoments(IM::AVERAGE) = s0 / nPts;
    calcMoments(IM::INTEGRATED) = s0 * integratedScaleFactor;
    if (abs(s0) > 0.0) {
        calcMoments(IM::WEIGHTED_MEAN_COORDINATE) = s1 / s0;
        calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE) =
            (s2 / s0) - calcMoments(IM::WEIGHTED_MEAN_COORDINATE) * calcMoments(IM::WEIGHTED_MEAN_COORDINATE);
        calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE) = abs(calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE));
        if (calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE) > 0.0) {
            calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE) = sqrt(calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE));
        } else {
            calcMoments(IM::WEIGHTED_DISPERSION_COORDINATE) = 0.0;
            calcMomentsMask(IM::WEIGHTED_DISPERSION_COORDINATE) = false;
        }
    } else {
        calcMomentsMask(IM::WEIGHTED_MEAN_COORDINATE) = false;
        calcMomentsMask(IM::WEIGHTED_DISPERSION_COORDINATE) = false;
    }

    // Standard deviation about mean of I
    if (nPts > 1 && casacore::Float((s0Sq - s0 * s0 / nPts) / (nPts - 1)) > 0) {
        calcMoments(IM::STANDARD_DEVIATION) = sqrt((s0Sq - s0 * s0 / nPts) / (nPts - 1));
    } else {
        calcMoments(IM::STANDARD_DEVIATION) = 0;
        calcMomentsMask(IM::STANDARD_DEVIATION) = false;
    }

    // Rms of I
    calcMoments(IM::RMS) = sqrt(s0Sq / nPts);

    // Absolute mean deviation
    calcMoments(IM::ABS_MEAN_DEVIATION) = sumAbsDev / nPts;

    // Maximum value
    calcMoments(IM::MAXIMUM) = dMax;

    // casacore::Coordinate of min/max value
    if (doCoord) {
        calcMoments(IM::MAXIMUM_COORDINATE) = getMomentCoord(iMom, pixelIn, worldOut, casacore::Double(iMax), iMom.convertToVelocity_p);
        calcMoments(IM::MINIMUM_COORDINATE) = getMomentCoord(iMom, pixelIn, worldOut, casacore::Double(iMin), iMom.convertToVelocity_p);
    } else {
        calcMoments(IM::MAXIMUM_COORDINATE) = 0.0;
        calcMoments(IM::MINIMUM_COORDINATE) = 0.0;
        calcMomentsMask(IM::MAXIMUM_COORDINATE) = false;
        calcMomentsMask(IM::MINIMUM_COORDINATE) = false;
    }

    // Minimum value
    calcMoments(IM::MINIMUM) = dMin;

    // Medians
    calcMoments(IM::MEDIAN) = dMedian;
    calcMoments(IM::MEDIAN_COORDINATE) = vMedian;
}

#endif // CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_TCC_