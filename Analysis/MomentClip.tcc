//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/MomentClip.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTCLIP_TCC_
#define CARTA_BACKEND_ANALYSIS_MOMENTCLIP_TCC_

using namespace carta;

template <class T>
MomentClip<T>::MomentClip(
    shared_ptr<casacore::Lattice<T>> pAncilliaryLattice, MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut)
    : _ancilliaryLattice(pAncilliaryLattice), iMom_p(iMom), os_p(os) {
    selectMoments_p = this->selectMoments(iMom_p);
    constructorCheck(calcMoments_p, calcMomentsMask_p, selectMoments_p, nLatticeOut);
    casacore::Int momAxis = this->momentAxis(iMom_p);

    if (_ancilliaryLattice) {
        sliceShape_p.resize(_ancilliaryLattice->ndim());
        sliceShape_p = 1;
        sliceShape_p(momAxis) = _ancilliaryLattice->shape()(momAxis);
    }

    this->selectRange(range_p, doInclude_p, doExclude_p, iMom_p);
    this->costlyMoments(iMom_p, doMedianI_p, doMedianV_p, doAbsDev_p);

    // Are we computing coordinate-dependent moments. If so precompute coordinate vector if moment axis separable
    this->setCoordinateSystem(cSys_p, iMom_p);
    this->doCoordCalc(doCoordProfile_p, doCoordRandom_p, iMom_p);
    this->setUpCoords(
        iMom_p, pixelIn_p, worldOut_p, sepWorldCoord_p, os_p, integratedScaleFactor_p, cSys_p, doCoordProfile_p, doCoordRandom_p);

    // What is the axis type of the moment axis
    momAxisType_p = this->momentAxisName(cSys_p, iMom_p);

    // Number of failed Gaussian fits
    nFailed_p = 0;
}

template <class T>
MomentClip<T>::~MomentClip() {}

template <class T>
void MomentClip<T>::process(
    T&, casacore::Bool&, const casacore::Vector<T>&, const casacore::Vector<casacore::Bool>&, const casacore::IPosition&) {
    ThrowCc("MomentClip<T>::process(Vector<T>&, IPosition&): not implemented");
}

template <class T>
void MomentClip<T>::multiProcess(casacore::Vector<T>& moments, casacore::Vector<casacore::Bool>& momentsMask,
    const casacore::Vector<T>& profileIn, const casacore::Vector<casacore::Bool>& profileInMask, const casacore::IPosition& inPos) {
    // The profile comes with its own mask (or a null mask which means all good).  In addition, we create a further mask by applying the
    // clip range to either the primary lattice, or the ancilliary lattice (e.g. the smoothed lattice). Fish out the ancilliary image slice
    // if needed. Stupid slice functions require me to create the slice empty every time so degenerate axes can be chucked out.  We set up a
    // pointer to the primary or ancilliary vector object  that we can use for fast access
    const T* pProfileSelect = nullptr;
    auto deleteIt = false;

    if (_ancilliaryLattice && (doInclude_p || doExclude_p)) {
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

    // Resize array for median.  Is resized correctly later
    auto nPts = profileIn.size();
    selectedData_p.resize(nPts);
    selectedDataIndex_p.resize(nPts);

    // Were the profile coordinates precomputed ?
    auto preComp = sepWorldCoord_p.nelements() > 0;

    // We must fill in the input pixel coordinate if we need coordinates, but did not pre compute them
    if (!preComp && (doCoordRandom_p || doCoordProfile_p)) {
        for (casacore::uInt i = 0; i < inPos.size(); ++i) {
            pixelIn_p[i] = casacore::Double(inPos[i]);
        }
    }

    // Compute moments.  The actual moment computation always done with the original data, regardless of whether the pixel selection is done
    // with the primary or ancilliary data.
    typename casacore::NumericTraits<T>::PrecisionType s0 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s0Sq = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s1 = 0.0;
    typename casacore::NumericTraits<T>::PrecisionType s2 = 0.0;

    casacore::Int iMin = -1;
    casacore::Int iMax = -1;
    T dMin = 1.0e30;
    T dMax = -1.0e30;
    casacore::Double coord = 0.0;
    casacore::uInt i, j;

    if (profileInMask.empty()) {
        // No mask included.
        if (doInclude_p) {
            for (i = 0, j = 0; i < nPts; ++i) {
                if (pProfileSelect[i] >= range_p[0] && pProfileSelect[i] <= range_p[1]) {
                    if (preComp) {
                        coord = sepWorldCoord_p(i);
                    } else if (doCoordProfile_p) {
                        coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                    }
                    this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn(i), coord);
                    selectedData_p[j] = profileIn[i];
                    selectedDataIndex_p[j] = i;
                    ++j;
                }
            }
            nPts = j;
        } else if (doExclude_p) {
            for (i = 0, j = 0; i < nPts; ++i) {
                if (pProfileSelect[i] <= range_p[0] || pProfileSelect[i] >= range_p[1]) {
                    if (preComp) {
                        coord = sepWorldCoord_p[i];
                    } else if (doCoordProfile_p) {
                        coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                    }
                    this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn[i], coord);
                    selectedData_p[j] = profileIn[i];
                    selectedDataIndex_p[j] = i;
                    ++j;
                }
            }
            nPts = j;
        } else {
            for (i = 0; i < nPts; ++i) {
                if (preComp) {
                    coord = sepWorldCoord_p[i];
                } else if (doCoordProfile_p) {
                    coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                }
                this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn[i], coord);
                selectedData_p[i] = profileIn[i];
                selectedDataIndex_p[i] = i;
            }
        }
    } else {
        // Set up a pointer for faster access to the profile mask
        auto deleteIt2 = false;
        const auto* pProfileInMask = profileInMask.getStorage(deleteIt2);

        if (doInclude_p) {
            for (i = 0, j = 0; i < nPts; ++i) {
                if (pProfileInMask[i] && pProfileSelect[i] >= range_p(0) && pProfileSelect[i] <= range_p(1)) {
                    if (preComp) {
                        coord = sepWorldCoord_p[i];
                    } else if (doCoordProfile_p) {
                        coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                    }
                    this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn(i), coord);
                    selectedData_p[j] = profileIn[i];
                    selectedDataIndex_p[j] = i;
                    ++j;
                }
            }
        } else if (doExclude_p) {
            for (i = 0, j = 0; i < nPts; ++i) {
                if (pProfileInMask[i] && (pProfileSelect[i] <= range_p[0] || pProfileSelect[i] >= range_p[1])) {
                    if (preComp) {
                        coord = sepWorldCoord_p(i);
                    } else if (doCoordProfile_p) {
                        coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                    }
                    this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn[i], coord);
                    selectedData_p[j] = profileIn[i];
                    selectedDataIndex_p[j] = i;
                    ++j;
                }
            }
        } else {
            for (i = 0, j = 0; i < nPts; ++i) {
                if (pProfileInMask[i]) {
                    if (preComp) {
                        coord = sepWorldCoord_p[i];
                    } else if (doCoordProfile_p) {
                        coord = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, casacore::Double(i));
                    }
                    this->accumSums(s0, s0Sq, s1, s2, iMin, iMax, dMin, dMax, i, profileIn[i], coord);
                    selectedData_p[j] = profileIn[i];
                    selectedDataIndex_p[j] = i;
                    ++j;
                }
            }
        }
        nPts = j;
        profileInMask.freeStorage(pProfileInMask, deleteIt2);
    }

    // Delete pointer memory
    if (_ancilliaryLattice && (doInclude_p || doExclude_p)) {
        ancilliarySliceRef_p.freeStorage(pProfileSelect, deleteIt);
    } else {
        profileIn.freeStorage(pProfileSelect, deleteIt);
    }

    // If no points make moments zero and mask
    if (nPts == 0) {
        moments = 0.0;
        momentsMask = false;
        return;
    }

    // Absolute deviations of I from mean needs an extra pass.
    typename casacore::NumericTraits<T>::PrecisionType sumAbsDev = 0;
    if (doAbsDev_p) {
        T iMean = s0 / nPts;
        for (i = 0; i < nPts; ++i) {
            sumAbsDev += abs(selectedData_p(i) - iMean);
        }
    }

    // Median of I
    T dMedian = 0.0;
    if (doMedianI_p) {
        selectedData_p.resize(nPts, true);
        dMedian = median(selectedData_p);
    }

    // Median coordinate. ImageMoments will only be allowing this if we are not offering the ancilliary lattice, and with an include
    // or exclude range. Pretty dodgy
    T vMedian(0.0);
    if (doMedianV_p) {
        if (doInclude_p || doExclude_p) {
            // Treat spectrum as a probability distribution for velocity
            // and generate cumulative probability (it's already sorted
            // of course).
            selectedData_p.resize(nPts, true);
            selectedData_p(0) = abs(selectedData_p[0]);
            auto dataMax = selectedData_p[0];
            for (i = 1; i < nPts; ++i) {
                selectedData_p[i] += abs(selectedData_p[i - 1]);
                dataMax = max(dataMax, selectedData_p[i]);
            }

            // Find 1/2 way value (well, the first one that occurs)
            auto halfMax = dataMax / 2.0;
            casacore::Int iVal = 0;
            for (i = 0; i < nPts; ++i) {
                if (selectedData_p[i] >= halfMax) {
                    iVal = i;
                    break;
                }
            }
            // Linearly interpolate to velocity index
            casacore::Double interpPixel;
            if (iVal > 0) {
                casacore::Double m =
                    (selectedData_p[iVal] - selectedData_p[iVal - 1]) / (selectedDataIndex_p[iVal] - selectedDataIndex_p[iVal - 1]);
                casacore::Double b = selectedData_p[iVal] - m * selectedDataIndex_p[iVal];
                interpPixel = (selectedData_p[iVal] - b) / m;
            } else {
                interpPixel = selectedDataIndex_p[iVal];
            }
            // Find world coordinate of that pixel on the moment axis
            vMedian = this->getMomentCoord(iMom_p, pixelIn_p, worldOut_p, interpPixel, iMom_p.shouldConvertToVelocity());
        }
    }

    // Fill all moments array
    this->setCalcMoments(iMom_p, calcMoments_p, calcMomentsMask_p, pixelIn_p, worldOut_p, doCoordRandom_p, integratedScaleFactor_p, dMedian,
        vMedian, nPts, s0, s1, s2, s0Sq, sumAbsDev, dMin, dMax, iMin, iMax);

    // Fill vector of selected moments
    for (i = 0; i < selectMoments_p.size(); ++i) {
        moments[i] = calcMoments_p(selectMoments_p[i]);
        momentsMask[i] = calcMomentsMask_p(selectMoments_p[i]);
    }
}

#endif // CARTA_BACKEND_ANALYSIS_MOMENTCLIP_TCC_