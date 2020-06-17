//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/MomentFit.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTFIT_TCC_
#define CARTA_BACKEND_ANALYSIS_MOMENTFIT_TCC_

using namespace carta;

template <class T>
MomentFit<T>::MomentFit(MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut) : iMom_p(iMom), os_p(os) {
    // Set moment selection vector
    selectMoments_p = this->selectMoments(iMom_p);

    // Set/check some dimensionality
    constructorCheck(calcMoments_p, calcMomentsMask_p, selectMoments_p, nLatticeOut);

    // this->yAutoMinMax(yMinAuto_p, yMaxAuto_p, iMom_p);

    // Are we computing the expensive moments ?
    this->costlyMoments(iMom_p, doMedianI_p, doMedianV_p, doAbsDev_p);

    // Are we computing coordinate-dependent moments.  If so
    // precompute coordinate vector if moment axis is separable
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
MomentFit<T>::~MomentFit() {}

template <class T>
void MomentFit<T>::process(
    T&, casacore::Bool&, const casacore::Vector<T>&, const casacore::Vector<casacore::Bool>&, const casacore::IPosition&) {
    throw(casacore::AipsError("MomentFit<T>::process not implemented"));
}

template <class T>
void MomentFit<T>::multiProcess(casacore::Vector<T>& moments, casacore::Vector<casacore::Bool>& momentsMask,
    const casacore::Vector<T>& profileIn, const casacore::Vector<casacore::Bool>& profileInMask, const casacore::IPosition& inPos) {
    // Generate moments from a Gaussian fit of this profile
    auto nPts = profileIn.size();
    casacore::Vector<T> gaussPars(4);
    abcissa_p.resize(nPts);
    indgen(abcissa_p);

    if (!this->getAutoGaussianFit(nFailed_p, gaussPars, abcissa_p, profileIn, profileInMask, peakSNR_p, stdDeviation_p)) {
        moments = 0;
        momentsMask = false;
        return;
    }

    // Were the profile coordinates precomputed ?
    auto preComp = sepWorldCoord_p.size() > 0;

    //
    // We must fill in the input pixel coordinate if we need
    // coordinates, but did not pre compute them
    //
    if (!preComp && (doCoordRandom_p || doCoordProfile_p)) {
        for (casacore::uInt i = 0; i < inPos.size(); ++i) {
            pixelIn_p[i] = casacore::Double(inPos[i]);
        }
    }

    // Set Gaussian functional values.  We reuse the same functional that
    // was used in the interactive fitting display process.
    gauss_p.setHeight(gaussPars(0));
    gauss_p.setCenter(gaussPars(1));
    gauss_p.setWidth(gaussPars(2));

    // Compute moments from the fitted Gaussian
    typename casacore::NumericTraits<T>::PrecisionType s0 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s0Sq = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s1 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s2 = 0.0;

    casacore::Int iMin = -1;
    casacore::Int iMax = -1;
    T dMin = 1.0e30;
    T dMax = -1.0e30;
    casacore::Double coord = 0.0;
    T xx;
    casacore::Vector<T> gData(nPts);

    casacore::uInt i, j;
    for (i = 0, j = 0; i < nPts; ++i) {
        if (profileInMask(i)) {
            xx = i;
            gData[j] = gauss_p(xx) + gaussPars[3];

            if (preComp) {
                coord = sepWorldCoord_p(i);
            } else if (doCoordProfile_p) {
                coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
            }
            this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, gData(j), coord);
            ++j;
        }
    }

    // If no unmasked points go home.  This shouldn't happen
    // as we can't have done a fit under these conditions.
    nPts = j;
    if (nPts == 0) {
        moments = 0;
        momentsMask = false;
        return;
    }

    // Absolute deviations of I from mean needs an extra pass.
    typename casacore::NumericTraits<T>::PrecisionType sumAbsDev = 0.0;
    if (doAbsDev_p) {
        T iMean = s0 / nPts;
        for (i = 0; i < nPts; ++i) {
            sumAbsDev += abs(gData[i] - iMean);
        }
    }

    // Median of I
    T dMedian = 0.0;
    if (doMedianI_p) {
        gData.resize(nPts, true);
        dMedian = median(gData);
    }
    T vMedian = 0.0;

    // Fill all moments array
    this->setCalcMoments(iMom_p, calcMoments_p, calcMomentsMask_p, pixelIn_p, worldOut_p, doCoordRandom_p, integratedScaleFactor_p, dMedian,
        vMedian, nPts, s0, s1, s2, s0Sq, sumAbsDev, dMin, dMax, iMin, iMax);

    // Fill vector of selected moments
    for (i = 0; i < selectMoments_p.size(); ++i) {
        moments[i] = calcMoments_p(selectMoments_p[i]);
        momentsMask[i] = true;
        momentsMask[i] = calcMomentsMask_p(selectMoments_p[i]);
    }
}

#endif // CARTA_BACKEND_ANALYSIS_MOMENTFIT_TCC_