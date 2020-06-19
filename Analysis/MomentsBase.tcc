//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/MomentsBase.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTSBASE_TCC_
#define CARTA_BACKEND_ANALYSIS_MOMENTSBASE_TCC_

using namespace carta;

template <class T>
MomentsBase<T>::MomentsBase(casacore::LogIO& os, casacore::Bool overWriteOutput, casacore::Bool showProgressU)
    : os_p(os), showProgress_p(showProgressU), overWriteOutput_p(overWriteOutput) {
    casacore::UnitMap::putUser("pix", casacore::UnitVal(1.0), "pixel units");
}

template <class T>
MomentsBase<T>::~MomentsBase() {
    // do nothing
}

template <class T>
casacore::Bool MomentsBase<T>::setMoments(const casacore::Vector<casacore::Int>& momentsU)
//
// Assign the desired moments
//
{
    if (!goodParameterStatus_p) {
        error_p = "Internal class status is bad";
        return false;
    }

    moments_p.resize(0);
    moments_p = momentsU;

    // Check number of moments

    casacore::uInt nMom = moments_p.nelements();
    if (nMom == 0) {
        error_p = "No moments requested";
        goodParameterStatus_p = false;
        return false;
    } else if (nMom > NMOMENTS) {
        error_p = "Too many moments specified";
        goodParameterStatus_p = false;
        return false;
    }

    for (casacore::uInt i = 0; i < nMom; i++) {
        if (moments_p(i) < 0 || moments_p(i) > NMOMENTS - 1) {
            error_p = "Illegal moment requested";
            goodParameterStatus_p = false;
            return false;
        }
    }
    return true;
}

template <class T>
casacore::Bool MomentsBase<T>::setWinFitMethod(const casacore::Vector<casacore::Int>& methodU)
//
// Assign the desired windowing and fitting methods
//
{
    if (!goodParameterStatus_p) {
        error_p = "Internal class status is bad";
        return false;
    }

    // No extra methods set

    if (methodU.nelements() == 0)
        return true;

    // Check legality

    for (casacore::uInt i = 0; i < casacore::uInt(methodU.nelements()); i++) {
        if (methodU(i) < 0 || methodU(i) > NMETHODS - 1) {
            error_p = "Illegal method given";
            goodParameterStatus_p = false;
            return false;
        }
    }

    // Assign Boooools

    linearSearch(doWindow_p, methodU, casacore::Int(WINDOW), methodU.nelements());
    linearSearch(doFit_p, methodU, casacore::Int(FIT), methodU.nelements());
    return true;
}

template <class T>
casacore::Bool MomentsBase<T>::setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxesU,
    const casacore::Vector<casacore::Int>& kernelTypesU, const casacore::Vector<casacore::Double>& kernelWidthsU) {
    const casacore::uInt n = kernelWidthsU.nelements();
    casacore::Vector<casacore::Quantum<casacore::Double>> t(n);
    for (casacore::uInt i = 0; i < n; i++) {
        t(i) = casacore::Quantum<casacore::Double>(kernelWidthsU(i), casacore::String("pix"));
    }
    return setSmoothMethod(smoothAxesU, kernelTypesU, t);
}

template <class T>
void MomentsBase<T>::setInExCludeRange(const casacore::Vector<T>& includeU, const casacore::Vector<T>& excludeU) {
    ThrowIf(!goodParameterStatus_p, "Internal class status is bad");
    _setIncludeExclude(selectRange_p, noInclude_p, noExclude_p, includeU, excludeU);
}

template <class T>
void MomentsBase<T>::setSnr(const T& peakSNRU, const T& stdDeviationU) {
    //
    // Assign the desired snr.  The default assigned in
    // the constructor is 3,0
    //
    ThrowIf(!goodParameterStatus_p, "Internal class status is bad");
    peakSNR_p = peakSNRU <= 0.0 ? T(3.0) : peakSNRU;
    stdDeviation_p = stdDeviationU <= 0.0 ? 0.0 : stdDeviationU;
}

template <class T>
casacore::Bool MomentsBase<T>::setSmoothOutName(const casacore::String& smoothOutU)
//
// Assign the desired smoothed image output file name
//
{
    if (!goodParameterStatus_p) {
        error_p = "Internal class status is bad";
        return false;
    }
    //
    if (!overWriteOutput_p) {
        casacore::NewFile x;
        casacore::String error;
        if (!x.valueOK(smoothOutU, error)) {
            return false;
        }
    }
    //
    smoothOut_p = smoothOutU;
    return true;
}

template <class T>
void MomentsBase<T>::setVelocityType(casacore::MDoppler::Types velocityType) {
    velocityType_p = velocityType;
}

template <class T>
casacore::Vector<casacore::Int> MomentsBase<T>::toMethodTypes(const casacore::String& methods)
//
// Helper function to convert a string containing a list of desired smoothed
// kernel types to the correct <src>casacore::Vector<casacore::Int></src>
// required for the <src>setSmooth</src> function.
//
// Inputs:
//   methods     SHould contain some of "win", "fit", "inter"
//
{
    casacore::Vector<casacore::Int> methodTypes(3);
    if (!methods.empty()) {
        casacore::String tMethods = methods;
        tMethods.upcase();

        casacore::Int i = 0;
        if (tMethods.contains("WIN")) {
            methodTypes(i) = WINDOW;
            i++;
        }
        if (tMethods.contains("FIT")) {
            methodTypes(i) = FIT;
            i++;
        }
        methodTypes.resize(i, true);
    } else {
        methodTypes.resize(0);
    }
    return methodTypes;
}

template <class T>
void MomentsBase<T>::_checkMethod() {
    using std::endl;

    // Only can have the median coordinate under certain conditions
    casacore::Bool found;
    if (linearSearch(found, moments_p, casacore::Int(MEDIAN_COORDINATE), moments_p.nelements()) != -1) {
        casacore::Bool noGood = false;
        if (doWindow_p || doFit_p || doSmooth_p) {
            noGood = true;
        } else {
            if (noInclude_p && noExclude_p) {
                noGood = true;
            } else {
                if (selectRange_p(0) * selectRange_p(1) < T(0)) {
                    noGood = true;
                }
            }
        }
        std::cerr << "Request for the median coordinate moment, but it is only "
                     "available with the basic (no smooth, no window, no fit) method "
                     "and a pixel range that is either all positive or all negative\n";
        // ThrowIf(noGood,
        //     "Request for the median coordinate moment, but it is only "
        //     "available with the basic (no smooth, no window, no fit) method "
        //     "and a pixel range that is either all positive or all negative");
    }
    // Now check all the silly methods
    if (!((!doSmooth_p && !doWindow_p && !doFit_p && (noInclude_p && noExclude_p)) ||
            (doSmooth_p && !doWindow_p && !doFit_p && (!noInclude_p || !noExclude_p)) ||
            (!doSmooth_p && !doWindow_p && !doFit_p && (!noInclude_p || !noExclude_p)) ||
            (doSmooth_p && doWindow_p && !doFit_p && (noInclude_p && noExclude_p)) ||
            (!doSmooth_p && doWindow_p && !doFit_p && (noInclude_p && noExclude_p)) ||
            (!doSmooth_p && doWindow_p && doFit_p && (noInclude_p && noExclude_p)) ||
            (doSmooth_p && doWindow_p && doFit_p && (noInclude_p && noExclude_p)) ||
            (!doSmooth_p && !doWindow_p && doFit_p && (noInclude_p && noExclude_p)))) {
        std::ostringstream oss;
        oss << "Invalid combination of methods requested." << endl;
        oss << "Valid combinations are: " << endl << endl;
        oss << "Smooth    Window      Fit   in/exclude " << endl;
        oss << "---------------------------------------" << endl;
        // Basic method. Just use all the data
        oss << "  N          N         N        N      " << endl;
        // casacore::Smooth and clip, or just clip
        oss << "  Y/N        N         N        Y      " << endl << endl;
        // Automatic windowing via Bosma's algorithm with or without smoothing
        oss << "  Y/N        Y         N        N      " << endl;
        // Windowing by fitting Gaussians (selecting +/- 3-sigma) automatically or
        // interactively with or without out smoothing
        oss << "  Y/N        Y         Y        N      " << endl;
        // Interactive and automatic Fitting of Gaussians and the moments worked out
        // directly from the fits
        oss << "  N          N         Y        N      " << endl << endl;

        oss << "Request was" << endl << endl;
        oss << "  " << (doSmooth_p ? "Y" : "N");
        oss << "          " << (doWindow_p ? "Y" : "N");
        oss << "         " << (doFit_p ? "Y" : "N");
        oss << "        " << (noInclude_p && noExclude_p ? "Y" : "N");
        oss << endl;
        oss << "-----------------------------------------------------" << endl;
        ThrowCc(oss.str());
    }

    // Tell them what they are getting
    os_p << endl
         << endl
         << "********************************************************************"
            "***"
         << endl;
    os_p << casacore::LogIO::NORMAL << "You have selected the following methods" << endl;
    if (doWindow_p) {
        os_p << "The window method" << endl;
        if (doFit_p) {
            os_p << "   with window selection via automatic Gaussian fitting" << endl;
        } else {
            os_p << "   with automatic window selection via the converging mean "
                    "(Bosma) algorithm"
                 << endl;
        }
        if (doSmooth_p) {
            os_p << "   operating on the smoothed image.  The moments are still" << endl;
            os_p << "   evaluated from the unsmoothed image" << endl;
        } else {
            os_p << "   operating on the unsmoothed image" << endl;
        }
    } else if (doFit_p) {
        os_p << "The automatic Gaussian fitting method" << endl;
        os_p << "   operating on the unsmoothed data" << endl;
        os_p << "   The moments are evaluated from the fits" << endl;
    } else if (doSmooth_p) {
        os_p << "The smooth and clip method.  The moments are evaluated from" << endl;
        os_p << "   the masked unsmoothed image" << endl;
    } else {
        if (noInclude_p && noExclude_p) {
            os_p << "The basic method" << endl;
        } else {
            os_p << "The basic clip method" << endl;
        }
    }
    os_p << endl << endl << casacore::LogIO::POST;
}

template <class T>
casacore::Bool MomentsBase<T>::_setOutThings(casacore::String& suffix, casacore::Unit& momentUnits, const casacore::Unit& imageUnits,
    const casacore::String& momentAxisUnits, const casacore::Int moment, casacore::Bool convertToVelocity) {
    // Set the output image suffixes and units
    //
    // casacore::Input:
    //   momentAxisUnits
    //                The units of the moment axis
    //   moment       The current selected moment
    //   imageUnits   The brightness units of the input image.
    //   convertToVelocity
    //                The moment axis is the spectral axis and
    //                world coordinates must be converted to km/s
    // Outputs:
    //   momentUnits  The brightness units of the moment image. Depends upon
    //   moment type suffix       suffix for output file name casacore::Bool true
    //   if could set units for moment image, false otherwise
    casacore::String temp;
    auto goodUnits = true;
    auto goodImageUnits = !imageUnits.getName().empty();
    auto goodAxisUnits = !momentAxisUnits.empty();

    if (moment == AVERAGE) {
        suffix = ".average";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == INTEGRATED) {
        suffix = ".integrated";
        temp = imageUnits.getName() + "." + momentAxisUnits;
        if (convertToVelocity) {
            temp = imageUnits.getName() + casacore::String(".km/s");
        }
        goodUnits = (goodImageUnits && goodAxisUnits);
    } else if (moment == WEIGHTED_MEAN_COORDINATE) {
        suffix = ".weighted_coord";
        temp = momentAxisUnits;
        if (convertToVelocity) {
            temp = casacore::String("km/s");
        }
        goodUnits = goodAxisUnits;
    } else if (moment == WEIGHTED_DISPERSION_COORDINATE) {
        suffix = ".weighted_dispersion_coord";
        temp = momentAxisUnits + "." + momentAxisUnits;
        if (convertToVelocity) {
            temp = casacore::String("km/s");
        }
        goodUnits = goodAxisUnits;
    } else if (moment == MEDIAN) {
        suffix = ".median";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == STANDARD_DEVIATION) {
        suffix = ".standard_deviation";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == RMS) {
        suffix = ".rms";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == ABS_MEAN_DEVIATION) {
        suffix = ".abs_mean_dev";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == MAXIMUM) {
        suffix = ".maximum";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == MAXIMUM_COORDINATE) {
        suffix = ".maximum_coord";
        temp = momentAxisUnits;
        if (convertToVelocity) {
            temp = casacore::String("km/s");
        }
        goodUnits = goodAxisUnits;
    } else if (moment == MINIMUM) {
        suffix = ".minimum";
        temp = imageUnits.getName();
        goodUnits = goodImageUnits;
    } else if (moment == MINIMUM_COORDINATE) {
        suffix = ".minimum_coord";
        temp = momentAxisUnits;
        if (convertToVelocity) {
            temp = casacore::String("km/s");
        }
        goodUnits = goodAxisUnits;
    } else if (moment == MEDIAN_COORDINATE) {
        suffix = ".median_coord";
        temp = momentAxisUnits;
        if (convertToVelocity) {
            temp = casacore::String("km/s");
        }
        goodUnits = goodAxisUnits;
    }
    if (goodUnits) {
        momentUnits.setName(temp);
    }
    return goodUnits;
}

template <class T>
void MomentsBase<T>::_setIncludeExclude(casacore::Vector<T>& range, casacore::Bool& noInclude, casacore::Bool& noExclude,
    const casacore::Vector<T>& include, const casacore::Vector<T>& exclude) {
    // Take the user's data inclusion and exclusion data ranges and
    // generate the range and Booleans to say what sort it is
    //
    // Inputs:
    //   include   Include range given by user. Zero length indicates
    //             no include range
    //   exclude   Exclude range given by user. As above.
    //   os        Output stream for reporting
    // Outputs:
    //   noInclude If true user did not give an include range
    //   noExclude If true user did not give an exclude range
    //   range     A pixel value selection range.  Will be resized to
    //             zero length if both noInclude and noExclude are true
    //   casacore::Bool      true if successfull, will fail if user tries to give
    //   too
    //             many values for includeB or excludeB, or tries to give
    //             values for both

    noInclude = true;
    range.resize(0);
    if (include.size() == 0) {
        // do nothing
    } else if (include.size() == 1) {
        range.resize(2);
        range(0) = -std::abs(include(0));
        range(1) = std::abs(include(0));
        noInclude = false;
    } else if (include.nelements() == 2) {
        range.resize(2);
        range(0) = casacore::min(include(0), include(1));
        range(1) = casacore::max(include(0), include(1));
        noInclude = false;
    } else {
        ThrowCc("Too many elements for argument include");
    }
    noExclude = true;
    if (exclude.size() == 0) {
        // do nothing
    } else if (exclude.nelements() == 1) {
        range.resize(2);
        range(0) = -std::abs(exclude(0));
        range(1) = std::abs(exclude(0));
        noExclude = false;
    } else if (exclude.nelements() == 2) {
        range.resize(2);
        range(0) = casacore::min(exclude(0), exclude(1));
        range(1) = casacore::max(exclude(0), exclude(1));
        noExclude = false;
    } else {
        ThrowCc("Too many elements for argument exclude");
    }
    if (!noInclude && !noExclude) {
        ThrowCc("You can only give one of arguments include or exclude");
    }
}

template <class T>
casacore::CoordinateSystem MomentsBase<T>::_makeOutputCoordinates(casacore::IPosition& outShape, const casacore::CoordinateSystem& cSysIn,
    const casacore::IPosition& inShape, casacore::Int momentAxis, casacore::Bool removeAxis) {
    casacore::CoordinateSystem cSysOut;
    cSysOut.setObsInfo(cSysIn.obsInfo());

    // Find the casacore::Coordinate corresponding to the moment axis

    casacore::Int coord, axisInCoord;
    cSysIn.findPixelAxis(coord, axisInCoord, momentAxis);
    const casacore::Coordinate& c = cSysIn.coordinate(coord);

    // Find the number of axes

    if (removeAxis) {
        // Shape with moment axis removed
        casacore::uInt dimIn = inShape.size();
        casacore::uInt dimOut = dimIn - 1;
        outShape.resize(dimOut);
        casacore::uInt k = 0;
        for (casacore::uInt i = 0; i < dimIn; ++i) {
            if (casacore::Int(i) != momentAxis) {
                outShape(k) = inShape(i);
                ++k;
            }
        }
        if (c.nPixelAxes() == 1 && c.nWorldAxes() == 1) {
            // We can physically remove the coordinate and axis
            for (casacore::uInt i = 0; i < cSysIn.nCoordinates(); ++i) {
                // If this coordinate is not the moment axis coordinate,
                // and it has not been virtually removed in the input
                // we add it to the output.  We don't cope with transposed
                // CoordinateSystems yet.
                auto pixelAxes = cSysIn.pixelAxes(i);
                auto worldAxes = cSysIn.worldAxes(i);
                if (casacore::Int(i) != coord && pixelAxes[0] >= 0 && worldAxes[0] >= 0) {
                    cSysOut.addCoordinate(cSysIn.coordinate(i));
                }
            }
        } else {
            // Remove just world and pixel axis but not the coordinate
            cSysOut = cSysIn;
            casacore::Int worldAxis = cSysOut.pixelAxisToWorldAxis(momentAxis);
            cSysOut.removeWorldAxis(worldAxis, cSysIn.referenceValue()(worldAxis));
        }
    } else {
        // Retain the casacore::Coordinate and give the moment axis  shape 1.
        outShape.resize(0);
        outShape = inShape;
        outShape(momentAxis) = 1;
        cSysOut = cSysIn;
    }
    return cSysOut;
}

#endif // CARTA_BACKEND_ANALYSIS_MOMENTSBASE_TCC_