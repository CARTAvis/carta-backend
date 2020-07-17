//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/MomentCalcBase.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_H_
#define CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_H_

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/BasicMath/Math.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Logging/LogIO.h>
#include <casacore/casa/Utilities/Assert.h>
#include <casacore/casa/aips.h>
#include <casacore/coordinates/Coordinates.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/lattices/LatticeMath/LatticeStatsBase.h>
#include <casacore/lattices/LatticeMath/LineCollapser.h>
#include <casacore/scimath/Fitting/NonLinearFitLM.h>
#include <casacore/scimath/Functionals/CompoundFunction.h>
#include <casacore/scimath/Functionals/Gaussian1D.h>
#include <casacore/scimath/Functionals/Polynomial.h>
#include <casacore/scimath/StatsFramework/ClassicalStatistics.h>
#include <casacore/scimath/StatsFramework/StatisticsTypes.h>

namespace carta {

// Forward declarations the template class MomentsBase
template <class T>
class MomentsBase;

template <class T>
class MomentCalcBase : public casacore::LineCollapser<T, T> {
public:
    using AccumType = typename casacore::NumericTraits<T>::PrecisionType;
    using DataIterator = typename casacore::Vector<T>::const_iterator;
    using MaskIterator = casacore::Vector<casacore::Bool>::const_iterator;

    virtual ~MomentCalcBase();

    // Returns the number of failed fits if doing fitting
    virtual inline casacore::uInt nFailedFits() const {
        return nFailed_p;
    }

protected:
    // A number of private data members are kept here in the base class as they are common to the derived classes. Since this class is
    // abstract, they have to be filled by the derived classes.

    // CoordinateSystem
    casacore::CoordinateSystem cSys_p;

    // This vector is a container for all the possible moments that can be calculated. They are in the order given by the MomentsBase
    // enum MomentTypes
    casacore::Vector<T> calcMoments_p;
    casacore::Vector<casacore::Bool> calcMomentsMask_p;

    // This vector tells us which elements of the calcMoments_p vector we wish to select
    casacore::Vector<casacore::Int> selectMoments_p;

    // Although the general philosophy of these classes is to compute all the posisble moments and then select the ones we want, some of
    // them are too expensive to calculate unless they are really wanted.  These are the median moments and those that require a second
    // pass.  These control Bools tell us whether we really want to compute the expensive ones.
    casacore::Bool doMedianI_p, doMedianV_p, doAbsDev_p;

    // These vectors are used to transform coordinates between pixel and world
    casacore::Vector<casacore::Double> pixelIn_p, worldOut_p;

    // All computations involving casacore::Coordinate conversions are relatively expensive These Bools signifies whether we need coordinate
    // calculations or not for the full profile, and for some occaisional calculations
    casacore::Bool doCoordProfile_p, doCoordRandom_p;

    // This vector houses the world coordinate values for the profile if it was from a separable axis. This means this vector can be pre
    // computed just once, instead of working out the coordinates for each profile (expensive). It should only be filled if doCoordCalc_p is
    // true
    casacore::Vector<casacore::Double> sepWorldCoord_p;

    // This vector is used to hold the abscissa values
    casacore::Vector<T> abcissa_p;

    // This string tells us the name of the moment axis (VELO or FREQ etc)
    casacore::String momAxisType_p;

    // This is the number of Gaussian fits that failed.
    casacore::uInt nFailed_p;

    // This scale factor is the increment along the moment axis applied so that units for the Integrated moment are like Jy/beam, km/s (or
    // whatever is needed for the moment axis units) For non-linear velocities (e.g. optical) this is approximate only and is computed at
    // the reference pixel
    casacore::Double integratedScaleFactor_p;

    // Accumulate statistical sums from a vector
    inline void accumSums(typename casacore::NumericTraits<T>::PrecisionType& s0, typename casacore::NumericTraits<T>::PrecisionType& s0Sq,
        typename casacore::NumericTraits<T>::PrecisionType& s1, typename casacore::NumericTraits<T>::PrecisionType& s2, casacore::Int& iMin,
        casacore::Int& iMax, T& dMin, T& dMax, casacore::Int i, T datum, casacore::Double coord) const {
        // Accumulate statistical sums from this datum
        //
        // casacore::Input:
        //   i              Index
        //   datum          Pixel value
        //   coord          casacore::Coordinate value on moment axis
        //
        // casacore::Input/output:
        //   iMin, iMax     index of dMin and dMax
        //   dMin, dMax     minimum and maximum value
        //
        // Output:
        //   s0             sum (I)
        //   s0Sq           sum (I*I)
        //   s1             sum (I*v)
        //   s2             sum (I*v*v)

        typename casacore::NumericTraits<T>::PrecisionType dDatum = datum;
        s0 += dDatum;
        s0Sq += dDatum * dDatum;
        s1 += dDatum * coord;
        s2 += dDatum * coord * coord;
        if (datum < dMin) {
            iMin = i;
            dMin = datum;
        }
        if (datum > dMax) {
            iMax = i;
            dMax = datum;
        }
    }

    // Determine if the spectrum is pure noise
    casacore::uInt allNoise(
        T& dMean, const casacore::Vector<T>& data, const casacore::Vector<casacore::Bool>& mask, T peakSNR, T stdDeviation) const;

    // Check validity of constructor inputs
    void constructorCheck(casacore::Vector<T>& calcMoments, casacore::Vector<casacore::Bool>& calcMomentsMask,
        const casacore::Vector<casacore::Int>& selectMoments, casacore::uInt nLatticeOut) const;

    // Find out from the selectMoments array whether we want to compute the more expensive moments
    void costlyMoments(MomentsBase<T>& iMom, casacore::Bool& doMedianI, casacore::Bool& doMedianV, casacore::Bool& doAbsDev) const;

    // Return the casacore::Bool saying whether we need to compute coordinates or not for the requested moments
    void doCoordCalc(casacore::Bool& doCoordProfile, casacore::Bool& doCoordRandom, const MomentsBase<T>& iMom) const;

    // Return the casacore::Bool from the ImageMoments or MSMoments object saying whether we are going to fit Gaussians to the profiles or
    // not.
    casacore::Bool doFit(const MomentsBase<T>& iMom) const;

    // Find the next masked or unmasked point in a vector
    casacore::Bool findNextDatum(casacore::uInt& iFound, const casacore::uInt& n, const casacore::Vector<casacore::Bool>& mask,
        const casacore::uInt& iStart, const casacore::Bool& findGood) const;

    // Fit a Gaussian to x and y arrays given guesses for the gaussian parameters
    casacore::Bool fitGaussian(casacore::uInt& nFailed, T& peak, T& pos, T& width, T& level, const casacore::Vector<T>& x,
        const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, T peakGuess, T posGuess, T widthGuess,
        T levelGuess) const;

    // Automatically fit a Gaussian to a spectrum, including finding the starting guesses.
    casacore::Bool getAutoGaussianFit(casacore::uInt& nFailed, casacore::Vector<T>& gaussPars, const casacore::Vector<T>& x,
        const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, T peakSNR, T stdDeviation) const;

    // Automatically work out a guess for the Gaussian parameters Returns false if all pixels masked.
    casacore::Bool getAutoGaussianGuess(T& peakGuess, T& posGuess, T& widthGuess, T& levelGuess, const casacore::Vector<T>& x,
        const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask) const;

    // Compute the world coordinate for the given moment axis pixel
    inline casacore::Double getMomentCoord(const MomentsBase<T>& iMom, casacore::Vector<casacore::Double>& pixelIn,
        casacore::Vector<casacore::Double>& worldOut, casacore::Double momentPixel, casacore::Bool asVelocity = false) const {
        // Find the value of the world coordinate on the moment axis
        // for the given moment axis pixel value.
        //
        // Input
        //   momentPixel   is the index in the profile extracted from the data
        //
        // casacore::Input/output
        //   pixelIn       Pixels to convert.  Must all be filled in except for
        //                 pixelIn(momentPixel).
        //   worldOut      casacore::Vector to hold result
        //
        // Should really return a casacore::Fallible as I don't check and see
        // if the coordinate transformation fails or not

        // Should really check the result is true, but for speed ...
        pixelIn[iMom.momentAxis_p] = momentPixel;
        cSys_p.toWorld(worldOut, pixelIn);
        if (asVelocity) {
            casacore::Double velocity;
            cSys_p.spectralCoordinate().frequencyToVelocity(velocity, worldOut(iMom.worldMomentAxis_p));
            return velocity;
        }
        return worldOut(iMom.worldMomentAxis_p);
    }

    // Examine a mask and determine how many segments of unmasked points it consists of.
    void lineSegments(casacore::uInt& nSeg, casacore::Vector<casacore::uInt>& start, casacore::Vector<casacore::uInt>& nPts,
        const casacore::Vector<casacore::Bool>& mask) const;

    // Return the moment axis from the ImageMoments object
    casacore::Int& momentAxis(MomentsBase<T>& iMom) const;

    // Return the name of the moment/profile axis
    casacore::String momentAxisName(const casacore::CoordinateSystem&, const MomentsBase<T>& iMom) const;

    // Return the peak SNR for determination of all noise spectra from the ImageMoments or MSMoments object
    T& peakSNR(MomentsBase<T>& iMom) const;

    // Return the selected pixel intensity range from the ImageMoments or MSMoments object and the Bools describing whether it is inclusion
    // or exclusion
    void selectRange(casacore::Vector<T>& pixelRange, casacore::Bool& doInclude, casacore::Bool& doExlude, MomentsBase<T>& iMom) const;

    // The MomentCalculators compute a vector of all possible moments. This function returns a vector which selects the desired moments from
    // that "all moment" vector.
    casacore::Vector<casacore::Int> selectMoments(MomentsBase<T>& iMom) const;

    // Fill the output moments array
    void setCalcMoments(const MomentsBase<T>& iMom, casacore::Vector<T>& calcMoments, casacore::Vector<casacore::Bool>& calcMomentsMask,
        casacore::Vector<casacore::Double>& pixelIn, casacore::Vector<casacore::Double>& worldOut, casacore::Bool doCoord,
        casacore::Double integratedScaleFactor, T dMedian, T vMedian, casacore::Int nPts,
        typename casacore::NumericTraits<T>::PrecisionType s0, typename casacore::NumericTraits<T>::PrecisionType s1,
        typename casacore::NumericTraits<T>::PrecisionType s2, typename casacore::NumericTraits<T>::PrecisionType s0Sq,
        typename casacore::NumericTraits<T>::PrecisionType sumAbsDev, T dMin, T dMax, casacore::Int iMin, casacore::Int iMax) const;

    // Fill a string with the position of the cursor
    void setPosLabel(casacore::String& title, const casacore::IPosition& pos) const;

    // Install casacore::CoordinateSystem and SpectralCoordinate in protected data members
    void setCoordinateSystem(casacore::CoordinateSystem& cSys, MomentsBase<T>& iMom);

    // Set up separable moment axis coordinate vector and conversion vectors if not separable
    void setUpCoords(const MomentsBase<T>& iMom, casacore::Vector<casacore::Double>& pixelIn, casacore::Vector<casacore::Double>& worldOut,
        casacore::Vector<casacore::Double>& sepWorldCoord, casacore::LogIO& os, casacore::Double& integratedScaleFactor,
        const casacore::CoordinateSystem& cSys, casacore::Bool doCoordProfile, casacore::Bool doCoordRandom) const;

    // Return standard deviation of image from ImageMoments or MSMoments object
    T& stdDeviation(MomentsBase<T>& iMom) const;

protected:
    // Check if #pixels is indeed 1.
    virtual void init(casacore::uInt nOutPixelsPerCollapse);
};

} // namespace carta

#include "MomentCalcBase.tcc"

#endif // CARTA_BACKEND_ANALYSIS_MOMENTCALCBASE_H_
