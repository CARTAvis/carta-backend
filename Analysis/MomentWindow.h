//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/MomentWindow.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_H_
#define CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_H_

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/BasicMath/Math.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Logging/LogIO.h>
#include <casacore/casa/Utilities/Assert.h>
#include <casacore/casa/aips.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/lattices/LatticeMath/LatticeStatsBase.h>
#include <casacore/lattices/LatticeMath/LineCollapser.h>
#include <casacore/scimath/Fitting/NonLinearFitLM.h>
#include <casacore/scimath/Functionals/CompoundFunction.h>
#include <casacore/scimath/Functionals/Gaussian1D.h>
#include <casacore/scimath/Functionals/Polynomial.h>
#include <casacore/scimath/Mathematics/NumericTraits.h>

#include "MomentCalcBase.h"
#include "MomentsBase.h"

namespace carta {

// <summary> Computes moments from a windowed profile </summary>
// <use visibility=export>
//
// <reviewed reviewer="" date="yyyy/mm/dd" tests="" demos="">
// </reviewed>
//
// <prerequisite>
//   <li> <linkto class="MomentsBase">MomentsBase</linkto>
//   <li> <linkto class="ImageMoments">ImageMoments</linkto>
//   <li> <linkto class="MSMoments">MSMoments</linkto>
//   <li> <linkto class="casacore::LatticeApply">casacore::LatticeApply</linkto>
//   <li> <linkto class="MomentCalcBase">MomentCalcBase</linkto>
//   <li> <linkto
//   class="casacore::LineCollapser">casacore::LineCollapser</linkto>
// </prerequisite>
//
// <synopsis>
//  This concrete class is derived from the abstract base class MomentCalcBase
//  which provides an interface layer to the ImageMoments or MSMoments driver
//  class. ImageMoments or MSMoments creates a MomentWindow object and passes it
//  to the LatticeApply function lineMultiApply.  This function iterates through
//  a given lattice, and invokes the <src>multiProcess</src> member function of
//  MomentWindow on each profile of pixels that it extracts from the input
//  lattice.  The <src>multiProcess</src> function returns a vector of moments
//  which are inserted into the output lattices also supplied to the
//  casacore::LatticeApply function.
//
//  MomentWindow computes moments from a subset of the pixels selected from  the
//  input profile.  This subset is a simple index range, or window.  The window
//  is selected, for each profile, that is thought to surround the spectral
//  feature of interest.  This window can be found from the primary lattice, or
//  from an ancilliary lattice (ImageMoments or MSMoments offers a smoothed
//  version of the primary lattice as the ancilliary lattice).  The moments are
//  always computed from primary lattice data.
//
//  For each profile, the window can be found either interactively or
//  automatically. There are two interactive methods.  Either you just mark the
//  window with the cursor, or you interactively fit a Gaussian to the profile
//  and the +/- 3-sigma window is returned.  There are two automatic methods.
//  Either Bosma's converging mean algorithm is used, or an automatically  fit
//  Gaussian +/- 3-sigma window is returned.
//
//  The constructor takes an MomentsBase object that is actually an ImageMoments
//  or an MSMoments object; the one that is constructing the MomentWindow object
//  of course.   There is much control information embodied in the state  of the
//  ImageMoments or MSMoments object.  This information is extracted by the
//  MomentCalcBase class and passed on to MomentWindow for consumption.
//
//  Note that the ancilliary lattice is only accessed if the pointer to it
//  is non zero.
//
//  See the <linkto class="MomentsBase">MomentsBase</linkto>,
//  <linkto class="ImageMoments">ImageMoments</linkto>, and
//  <linkto class="MSMoments">MSMoments</linkto>
//  for discussion about the moments that are available for computation.
//
// </synopsis>
//
// <example>
// This example comes from ImageMoments.   outPt is a pointer block holding
// pointers to the output lattices.  The ancilliary masking lattice is
// just a smoothed version of the input lattice.  os_P is a casacore::LogIO
// object.
//
// <srcBlock>
//
//// Construct desired moment calculator object.  Use it polymorphically via
//// a pointer to the base class.
//
//   MomentCalcBase<T>* pMomentCalculator = 0;
//   if (clipMethod || smoothClipMethod) {
//      pMomentCalculator = new MomentClip<T>(pSmoothedImage, *this, os_p,
//      outPt.nelements());
//   } else if (windowMethod) {
//      pMomentCalculator = new MomentWindow<T>(pSmoothedImage, *this, os_p,
//      outPt.nelements());
//   } else if (fitMethod) {
//      pMomentCalculator = new MomentFit<T>(*this, os_p, outPt.nelements());
//   }
//
//// Iterate optimally through the image, compute the moments, fill the output
/// lattices
//
//   casacore::LatticeApply<T>::lineMultiApply(outPt, *pInImage_p,
//   *pMomentCalculator,
//                                   momentAxis_p, pProgressMeter);
//   delete pMomentCalculator;
//
// </srcBlock>
// </example>
//
// <motivation>
// </motivation>
//
// <note role=tip>
// Note that there are is assignment operator or copy constructor.
// Do not use the ones the system would generate either.
// </note>
//
// <todo asof="yyyy/mm/dd">
// </todo>

template <class T>
class MomentWindow : public MomentCalcBase<T> {
public:
    using AccumType = typename casacore::NumericTraits<T>::PrecisionType;
    using DataIterator = typename casacore::Vector<T>::const_iterator;
    using MaskIterator = casacore::Vector<casacore::Bool>::const_iterator;

    // Constructor.  The pointer is to a lattice containing the masking
    // lattice (created by ImageMoments or MSMoments).   We also need the
    // ImageMoments or MSMoments object which is calling us, its logger,
    // and the number of output lattices it has created.
    MomentWindow(
        shared_ptr<casacore::Lattice<T>> pAncilliaryLattice, MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut);

    // Destructor (does nothing).
    ~MomentWindow();

    // This function is not implemented and throws an exception.
    virtual void process(T& out, casacore::Bool& outMask, const casacore::Vector<T>& in, const casacore::Vector<casacore::Bool>& inMask,
        const casacore::IPosition& pos);

    // This function returns a vector of numbers from each input vector.
    // the output vector contains the moments known to the ImageMoments
    // or MSMoments object passed into the constructor.
    virtual void multiProcess(casacore::Vector<T>& out, casacore::Vector<casacore::Bool>& outMask, const casacore::Vector<T>& in,
        const casacore::Vector<casacore::Bool>& inMask, const casacore::IPosition& pos);

private:
    shared_ptr<casacore::Lattice<T>> _ancilliaryLattice;
    MomentsBase<T>& iMom_p;
    casacore::LogIO os_p;

    const casacore::Vector<T>* pProfileSelect_p;
    casacore::Vector<T> ancilliarySliceRef_p;
    casacore::Vector<T> selectedData_p;
    T stdDeviation_p, peakSNR_p;
    casacore::Bool doFit_p;
    casacore::IPosition sliceShape_p;

    // Automatically determine the spectral window
    casacore::Bool getAutoWindow(casacore::uInt& nFailed, casacore::Vector<casacore::Int>& window, const casacore::Vector<T>& x,
        const casacore::Vector<T>& y, const casacore::Vector<casacore::Bool>& mask, const T peakSNR, const T stdDeviation,
        const casacore::Bool doFit) const;

    // Automatically determine the spectral window via Bosma's algorithm
    casacore::Bool _getBosmaWindow(casacore::Vector<casacore::Int>& window, const casacore::Vector<T>& y,
        const casacore::Vector<casacore::Bool>& mask, const T peakSNR, const T stdDeviation) const;

    // Take the fitted Gaussian parameters and set an N-sigma window.
    // If the window is too small return a Fail condition.
    casacore::Bool setNSigmaWindow(
        casacore::Vector<casacore::Int>& window, const T pos, const T width, const casacore::Int nPts, const casacore::Int N) const;

    //# Make members of parent class known.
protected:
    using MomentCalcBase<T>::constructorCheck;
    using MomentCalcBase<T>::setPosLabel;
    // using MomentCalcBase<T>::convertF;
    using MomentCalcBase<T>::selectMoments_p;
    using MomentCalcBase<T>::calcMoments_p;
    using MomentCalcBase<T>::calcMomentsMask_p;
    using MomentCalcBase<T>::doMedianI_p;
    using MomentCalcBase<T>::doMedianV_p;
    using MomentCalcBase<T>::doAbsDev_p;
    using MomentCalcBase<T>::cSys_p;
    using MomentCalcBase<T>::doCoordProfile_p;
    using MomentCalcBase<T>::doCoordRandom_p;
    using MomentCalcBase<T>::pixelIn_p;
    using MomentCalcBase<T>::worldOut_p;
    using MomentCalcBase<T>::sepWorldCoord_p;
    using MomentCalcBase<T>::integratedScaleFactor_p;
    using MomentCalcBase<T>::momAxisType_p;
    using MomentCalcBase<T>::nFailed_p;
    using MomentCalcBase<T>::abcissa_p;
};

} // namespace carta

#include "MomentWindow.tcc"

#endif // CARTA_BACKEND_ANALYSIS_MOMENTWINDOW_H_
