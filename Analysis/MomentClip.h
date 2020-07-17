//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/MomentClip.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTCLIP_H_
#define CARTA_BACKEND_ANALYSIS_MOMENTCLIP_H_

#include "MomentCalcBase.h"

namespace carta {

// <summary> Computes simple clipped, and masked moments</summary>
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
//  ImageMoments or MSMoments creates a MomentClip object and passes it to the LatticeApply
//  function, lineMultiApply. This function iterates through a given lattice,
//  and invokes the <src>multiProcess</src> member function of MomentClip on each vector
//  of pixels that it extracts from the input lattice.  The <src>multiProcess</src>
//  function returns a vector of moments which are inserted into the output
//  lattices also supplied to the casacore::LatticeApply function.
//
//  MomentClip computes moments directly from a vector of pixel intensities
//  extracted from the primary lattice.  An optional pixel intensity inclusion
//  or exclusion range can be applied.   It can also compute a mask based on the
//  inclusion or exclusion ranges applied to an ancilliary lattice (the ancilliary
//  vector corresponding to the primary vector is extracted).  This mask is then
//  applied to the primary vector for moment computation (ImageMoments or MSMoments offers
//  a smoothed version of the primary lattice as the ancilliary lattice)
//
//  The constructor takes an MomentsBase object that is actually an ImageMoments or
//  an MSMoments object; the one that is constructing
//  the MomentClip object of course.   There is much control information embodied
//  in the state of the ImageMoments or MSMoments object.  This information is extracted by the
//  MomentCalcBase class and passed on to MomentClip for consumption.
//
//  Note that the ancilliary lattice is only accessed if the ImageMoments or MSMoments
//  object indicates that a pixel inclusion or exclusion range has been
//  given as well as the pointer to the lattice having a non-zero value.
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
// just a smoothed version of the input lattice.
//
// <srcBlock>
//
//// Construct desired moment calculator object.  Use it polymorphically
//// via a pointer to the base class.  os_P is a casacore::LogIO object.
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
//
// </srcBlock>
// </example>
//
// <note role=tip>
// Note that there are is assignment operator or copy constructor.
// Do not use the ones the system would generate either.
// </note>
//
// <todo asof="yyyy/mm/dd">
// </todo>

template <class T>
class MomentClip : public MomentCalcBase<T> {
public:
    // Constructor.  The pointer is to an ancilliary  lattice used as a mask. If no masking lattice is desired, the pointer value must be
    // zero.  We also need the ImageMoments or MSMoments object which is calling us, its logger, and the number of output lattices it has
    // created.
    MomentClip(
        shared_ptr<casacore::Lattice<T>> pAncilliaryLattice, MomentsBase<T>& iMom, casacore::LogIO& os, const casacore::uInt nLatticeOut);

    // Destructor (does nothing).
    virtual ~MomentClip();

    // This function is not implemented and throws an exception.
    virtual void process(T& out, casacore::Bool& outMask, const casacore::Vector<T>& in, const casacore::Vector<casacore::Bool>& inMask,
        const casacore::IPosition& pos);

    // This function returns a vector of numbers from each input vector. the output vector contains the moments known to the ImageMoments
    // or MSMoments object passed into the constructor.
    virtual void multiProcess(casacore::Vector<T>& out, casacore::Vector<casacore::Bool>& outMask, const casacore::Vector<T>& in,
        const casacore::Vector<casacore::Bool>& inMask, const casacore::IPosition& pos);

    // Can handle null mask
    virtual casacore::Bool canHandleNullMask() const {
        return true;
    };

private:
    shared_ptr<casacore::Lattice<T>> _ancilliaryLattice;
    MomentsBase<T>& iMom_p;
    casacore::LogIO os_p;

    const casacore::Vector<T>* pProfileSelect_p = nullptr;
    casacore::Vector<T> ancilliarySliceRef_p;
    casacore::Vector<T> selectedData_p;
    casacore::Vector<casacore::Int> selectedDataIndex_p;
    casacore::Bool doInclude_p, doExclude_p;
    casacore::Vector<T> range_p;
    casacore::IPosition sliceShape_p;

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
};

} // namespace carta

#include "MomentClip.tcc"

#endif // CARTA_BACKEND_ANALYSIS_MOMENTCLIP_H_
