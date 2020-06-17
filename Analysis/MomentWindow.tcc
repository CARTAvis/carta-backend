//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/MomentWindow.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_TCC_
#define CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_TCC_

using namespace carta;

template <class T>
MomentWindow<T>::MomentWindow(
    shared_ptr<casacore::Lattice<T>> pAncilliaryLattice, MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut)
    : _ancilliaryLattice(pAncilliaryLattice), iMom_p(iMom), os_p(os) {
    // Set moment selection vector
    selectMoments_p = this->selectMoments(iMom_p);

    // Set/check some dimensionality
    constructorCheck(calcMoments_p, calcMomentsMask_p, selectMoments_p, nLatticeOut);

    // Fish out moment axis
    casacore::Int momAxis = this->momentAxis(iMom_p);

    // Set up slice shape for extraction from masking lattice
    if (_ancilliaryLattice != 0) {
        sliceShape_p.resize(_ancilliaryLattice->ndim());
        sliceShape_p = 1;
        sliceShape_p(momAxis) = _ancilliaryLattice->shape()(momAxis);
    }

    // this->yAutoMinMax(yMinAuto_p, yMaxAuto_p, iMom_p);

    // Are we computing the expensive moments ?
    this->costlyMoments(iMom_p, doMedianI_p, doMedianV_p, doAbsDev_p);

    // Are we computing coordinate-dependent moments.  If
    // so precompute coordinate vector is momebt axis separable
    this->setCoordinateSystem(cSys_p, iMom_p);
    this->doCoordCalc(doCoordProfile_p, doCoordRandom_p, iMom_p);
    this->setUpCoords(
        iMom_p, pixelIn_p, worldOut_p, sepWorldCoord_p, os_p, integratedScaleFactor_p, cSys_p, doCoordProfile_p, doCoordRandom_p);

    // What is the axis type of the moment axis
    momAxisType_p = this->momentAxisName(cSys_p, iMom_p);

    // Are we fitting, automatically or interactively ?
    doFit_p = this->doFit(iMom_p);

    // Values to assess if spectrum is all noise or not
    peakSNR_p = this->peakSNR(iMom_p);
    stdDeviation_p = this->stdDeviation(iMom_p);

    // Number of failed Gaussian fits
    nFailed_p = 0;
}

template <class T>
MomentWindow<T>::~MomentWindow() {}

template <class T>
void MomentWindow<T>::process(
    T&, casacore::Bool&, const casacore::Vector<T>&, const casacore::Vector<casacore::Bool>&, const casacore::IPosition&) {
    throw(casacore::AipsError("MomentWindow<T>::process not implemented"));
}

template <class T>
void MomentWindow<T>::multiProcess(casacore::Vector<T>& moments, casacore::Vector<casacore::Bool>& momentsMask,
    const casacore::Vector<T>& profileIn, const casacore::Vector<casacore::Bool>& profileInMask, const casacore::IPosition& inPos) {
    //
    // Generate windowed moments of this profile.
    // The profile comes with its own mask (or a null mask
    // which means all good).  In addition, we create
    // a further mask by applying the clip range to either
    // the primary lattice, or the ancilliary lattice (e.g.
    // the smoothed lattice)
    //

    // Fish out the ancilliary image slice if needed.  Stupid slice functions
    // require me to create the slice empty every time so degenerate
    // axes can be chucked out.  We set up a pointer to the primary or
    // ancilliary vector object  that we can use for fast access
    const T* pProfileSelect = 0;
    casacore::Bool deleteIt;
    if (_ancilliaryLattice) {
        casacore::Array<T> ancilliarySlice;
        casacore::IPosition stride(_ancilliaryLattice->ndim(), 1);
        _ancilliaryLattice->getSlice(ancilliarySlice, inPos, sliceShape_p, stride, true);
        ancilliarySliceRef_p.reference(ancilliarySlice);

        pProfileSelect_p = &ancilliarySliceRef_p;
        pProfileSelect = ancilliarySliceRef_p.getStorage(deleteIt);
    } else {
        pProfileSelect_p = &profileIn;
        pProfileSelect = profileIn.getStorage(deleteIt);
    }

    // Make abcissa and labels
    static casacore::Vector<casacore::Int> window(2);
    static casacore::Int nPts = 0;

    abcissa_p.resize(pProfileSelect_p->size());
    indgen(abcissa_p);

    // this->makeAbcissa (abcissa_p, pProfileSelect_p->nelements());

    casacore::String xLabel;
    if (momAxisType_p.empty()) {
        xLabel = "x (pixels)";
    } else {
        xLabel = momAxisType_p + " (pixels)";
    }
    const casacore::String yLabel("Intensity");
    casacore::String title;
    setPosLabel(title, inPos);

    // Do the window selection

    // Define the window automatically
    casacore::Vector<T> gaussPars;
    if (getAutoWindow(nFailed_p, window, abcissa_p, *pProfileSelect_p, profileInMask, peakSNR_p, stdDeviation_p, doFit_p)) {
        nPts = window(1) - window(0) + 1;
    } else {
        nPts = 0;
    }

    // If no points make moments zero and mask
    if (nPts == 0) {
        moments = 0.0;
        momentsMask = false;

        if (_ancilliaryLattice) {
            ancilliarySliceRef_p.freeStorage(pProfileSelect, deleteIt);
        } else {
            profileIn.freeStorage(pProfileSelect, deleteIt);
        }
        return;
    }

    // Resize array for median.  Is resized correctly later
    selectedData_p.resize(nPts);

    // Were the profile coordinates precomputed ?
    casacore::Bool preComp = (sepWorldCoord_p.nelements() > 0);

    //
    // We must fill in the input pixel coordinate if we need
    // coordinates, but did not pre compute them
    //
    if (!preComp) {
        if (doCoordRandom_p || doCoordProfile_p) {
            for (casacore::uInt i = 0; i < inPos.nelements(); i++) {
                pixelIn_p(i) = casacore::Double(inPos(i));
            }
        }
    }

    // Set up a pointer for fast access to the profile mask
    // if it exists.
    casacore::Bool deleteIt2;
    const casacore::Bool* pProfileInMask = profileInMask.getStorage(deleteIt2);

    // Accumulate sums and acquire selected data from primary lattice
    typename casacore::NumericTraits<T>::PrecisionType s0 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s0Sq = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s1 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s2 = 0.0;
    casacore::Int iMin = -1;
    casacore::Int iMax = -1;
    T dMin = 1.0e30;
    T dMax = -1.0e30;
    casacore::Double coord = 0.0;

    casacore::Int i, j;
    for (i = window(0), j = 0; i <= window(1); i++) {
        if (pProfileInMask[i]) {
            if (preComp) {
                coord = sepWorldCoord_p(i);
            } else if (doCoordProfile_p) {
                coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
            }
            this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn(i), coord);
            selectedData_p(j) = profileIn(i);
            j++;
        }
    }
    nPts = j;

    // Absolute deviations of I from mean needs an extra pass.
    typename casacore::NumericTraits<T>::PrecisionType sumAbsDev = 0.0;
    if (doAbsDev_p) {
        T iMean = s0 / nPts;
        for (i = 0; i < nPts; i++)
            sumAbsDev += abs(selectedData_p(i) - iMean);
    }

    // Delete memory associated with pointers
    if (_ancilliaryLattice) {
        ancilliarySliceRef_p.freeStorage(pProfileSelect, deleteIt);
    } else {
        profileIn.freeStorage(pProfileSelect, deleteIt);
    }
    profileInMask.freeStorage(pProfileInMask, deleteIt2);

    // Median of I
    T dMedian = 0.0;
    if (doMedianI_p) {
        selectedData_p.resize(nPts, true);
        dMedian = median(selectedData_p);
    }

    // Fill all moments array
    T vMedian = 0;
    this->setCalcMoments(iMom_p, calcMoments_p, calcMomentsMask_p, pixelIn_p, worldOut_p, doCoordRandom_p, integratedScaleFactor_p, dMedian,
        vMedian, nPts, s0, s1, s2, s0Sq, sumAbsDev, dMin, dMax, iMin, iMax);

    // Fill selected moments
    for (i = 0; i < casacore::Int(selectMoments_p.nelements()); i++) {
        moments(i) = calcMoments_p(selectMoments_p(i));
        momentsMask(i) = true;
        momentsMask(i) = calcMomentsMask_p(selectMoments_p(i));
    }
}

//
// Automatically fit a Gaussian and return the +/- 3-sigma window or
// invoke Bosma's method to set a window.  If a plotting device is
// active, we also plot the spectra and fits
//
// Inputs:
//   x,y        Spectrum
//   mask       Mask associated with spectrum. true is good.
//   plotter    Plot spectrum and optionally the  window
//   x,yLabel   x label for plots
//   title
// casacore::Input/output
//   nFailed    Cumulative number of failed fits
// Output:
//   window     The window (pixels).  If both 0,  then discard this spectrum
//              and mask moments
//
template <class T>
casacore::Bool MomentWindow<T>::getAutoWindow(casacore::uInt& nFailed, casacore::Vector<casacore::Int>& window,
    const casacore::Vector<T>& x, const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, const T peakSNR,
    const T stdDeviation, const casacore::Bool doFit) const {
    if (doFit) {
        casacore::Vector<T> gaussPars(4);
        if (!this->getAutoGaussianFit(nFailed, gaussPars, x, y, mask, peakSNR, stdDeviation)) {
            window = 0;
            return false;
        } else {
            // Set 3-sigma limits.  This assumes that there are some unmasked
            // points in the window !

            if (!setNSigmaWindow(window, gaussPars(1), gaussPars(2), y.nelements(), 3)) {
                window = 0;
                return false;
            }
        }
    } else {
        // Invoke Albert's method (see AJ, 86, 1791)

        if (!_getBosmaWindow(window, y, mask, peakSNR, stdDeviation)) {
            window = 0;
            return false;
        }
    }
    return true;
}

// Automatically work out the spectral window
// with Albert Bosma's algorithm.
// Inputs:
//   x,y       Spectrum
// Output:
//   window    The window
//   casacore::Bool      false if we reject this spectrum.  This may
//             be because it is all noise, or all masked
template <class T>
casacore::Bool MomentWindow<T>::_getBosmaWindow(casacore::Vector<casacore::Int>& window, const casacore::Vector<T>& y,
    const casacore::Vector<casacore::Bool>& mask, const T peakSNR, const T stdDeviation) const {
    // See if this spectrum is all noise first.  If so, forget it.
    // Return straight away if all maske
    T dMean;
    casacore::uInt iNoise = this->allNoise(dMean, y, mask, peakSNR, stdDeviation);
    if (iNoise == 2) {
        // all masked
        return false;
    }

    if (iNoise == 1) {
        // all noise
        window = 0;
        return false;
    }

    // Find peak
    casacore::ClassicalStatistics<AccumType, DataIterator, MaskIterator> statsCalculator;
    statsCalculator.addData(y.begin(), mask.begin(), y.size());
    casacore::StatsData<AccumType> stats = statsCalculator.getStatistics();
    const casacore::Int nPts = y.size();
    auto maxPos = stats.maxpos.second;
    casacore::Int iMin = max(0, casacore::Int(maxPos) - 2);
    casacore::Int iMax = min(nPts - 1, casacore::Int(maxPos) + 2);
    T tol = stdDeviation / (nPts - (iMax - iMin - 1));

    // Iterate to convergence
    auto first = true;
    auto converged = false;
    auto more = true;
    T yMean = 0;
    T oldYMean = 0;

    while (more) {
        // Find mean outside of peak region
        AccumType sum = 0;
        casacore::Int i, j;
        for (i = 0, j = 0; i < nPts; ++i) {
            if (mask[i] && (i < iMin || i > iMax)) {
                sum += y[i];
                ++j;
            }
        }
        if (j > 0) {
            yMean = sum / j;
        }

        // Interpret result
        if (!first && j > 0 && abs(yMean - oldYMean) < tol) {
            converged = true;
            more = false;
        } else if (iMin == 0 && iMax == nPts - 1) {
            more = false;
        } else {
            // Widen window and redetermine tolerance
            oldYMean = yMean;
            iMin = max(0, iMin - 2);
            iMax = min(nPts - 1, iMax + 2);
            tol = stdDeviation / (nPts - (iMax - iMin - 1));
        }
        first = false;
    }

    // Return window
    if (converged) {
        window[0] = iMin;
        window[1] = iMax;
        return true;
    } else {
        window = 0;
        return false;
    }
}

//
// Take the fitted Gaussian position and width and
// set an N-sigma window.  If the window is too small
// return a Fail condition.
//
// Inputs:
//   pos,width   The position and width in pixels
//   nPts        The number of points in the spectrum that was fit
//   N           The N-sigma
// Outputs:
//   window      The window in pixels
//   casacore::Bool        false if window too small to be sensible
//
template <class T>
casacore::Bool MomentWindow<T>::setNSigmaWindow(
    casacore::Vector<casacore::Int>& window, const T pos, const T width, const casacore::Int nPts, const casacore::Int N) const {
    window(0) = casacore::Int((pos - N * width) + 0.5);
    window(0) = min(nPts - 1, max(0, window(0)));
    window(1) = casacore::Int((pos + N * width) + 0.5);
    window(1) = min(nPts - 1, max(0, window(1)));
    // FIXME this was
    // if ( abs(window(1)-window(0)) < 3) return false;
    // return true;
    // but because window(1) - window(0) could be negative and true could be
    // returned, an allocation error was occuring because in another function a
    // vector was being resized to (window(1) - window(0)). It is possible that
    // in that case the absolute value should be calculated but I don't have time
    // at the moment to trace through the code and make sure that is really the
    // correct thing to do. Thus, making this function return false if window(1) -
    // window(0) seems the more conservative approach, so I'm doing that for now.
    return window(1) - window(0) >= 3;
}

#endif // CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_TCC_