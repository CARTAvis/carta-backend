//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/MomentFit.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTFIT_H_
#define CARTA_BACKEND_ANALYSIS_MOMENTFIT_H_

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

namespace carta {

// <summary> Compute moments from a Gaussian fitted to a profile</summary>
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
//   <li> <linkto class="casacore::LineCollapser">casacore::LineCollapser</linkto>
// </prerequisite>
//
// <synopsis>
//  This concrete class is derived from the abstract base class MomentCalcBase
//  which provides an interface layer to the ImageMoments or MSMoments driver class.
//  ImageMoments or MSMoments creates a MomentFit object and passes it to the LatticeApply
//  function, lineMultiApply. This function iterates through a given lattice,
//  and invokes the <src>multiProcess</src> member function of MomentFit on each vector
//  of pixels that it extracts from the input lattice.  The <src>multiProcess</src>
//  function returns a vector of moments which are inserted into the output
//  lattices also supplied to the casacore::LatticeApply function.
//
//  MomentFit computes moments by fitting a Gaussian to each profile.  The
//  moments are then computed from that fit.   The fitting can be done either
//  interactively or automatically.
//
//  The constructor takes MomentsBase object that is actually an ImageMoments or
//  an MSMoments object; the one that is constructing
//  the MomentFit object of course.   There is much control information embodied
//  in the state of the ImageMoments object.  This information is extracted by the
//  MomentCalcBase class and passed on to MomentFit for consumption.
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
// pointers to the output lattices.   os_P is a casacore::LogIO object.
//
// <srcBlock>
//
//// Construct desired moment calculator object.  Use it polymorphically via
//// a pointer to the base class.
//
//   MomentCalcBase<T>* pMomentCalculator = 0;
//   if (clipMethod || smoothClipMethod) {
//      pMomentCalculator = new MomentClip<T>(pSmoothedImage, *this, os_p, outPt.nelements());
//   } else if (windowMethod) {
//      pMomentCalculator = new MomentWindow<T>(pSmoothedImage, *this, os_p, outPt.nelements());
//   } else if (fitMethod) {
//      pMomentCalculator = new MomentFit<T>(*this, os_p, outPt.nelements());
//   }
//
//// Iterate optimally through the image, compute the moments, fill the output lattices
//
//   casacore::LatticeApply<T>::lineMultiApply(outPt, *pInImage_p, *pMomentCalculator,
//                                   momentAxis_p, pProgressMeter);
//   delete pMomentCalculator;
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
class MomentFit : public MomentCalcBase<T> {
public:
    // Constructor.  We need the ImageMoments or MSMoments object which is calling us,
    // its logger, and the number of output lattices it has created.
    MomentFit(MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut);

    // Destructor (does nothing).
    virtual ~MomentFit();

    // This function is not implemented and throws an exception.
    virtual void process(T& out, casacore::Bool& outMask, const casacore::Vector<T>& in, const casacore::Vector<casacore::Bool>& inMask,
        const casacore::IPosition& pos);

    // This function returns a vector of numbers from each input vector.
    // the output vector contains the moments known to the ImageMoments
    // or MSMoments object passed into the constructor.
    virtual void multiProcess(casacore::Vector<T>& out, casacore::Vector<casacore::Bool>& outMask, const casacore::Vector<T>& in,
        const casacore::Vector<casacore::Bool>& inMask, const casacore::IPosition& pos);

private:
    MomentsBase<T>& iMom_p;
    casacore::LogIO os_p;
    T stdDeviation_p, peakSNR_p;
    casacore::Bool doFit_p;
    casacore::Gaussian1D<T> gauss_p;

    //# Make members of parent class known.
protected:
    using MomentCalcBase<T>::constructorCheck;
    using MomentCalcBase<T>::setPosLabel;
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

#include "MomentFit.tcc"

#endif // CARTA_BACKEND_ANALYSIS_MOMENTFIT_H_
