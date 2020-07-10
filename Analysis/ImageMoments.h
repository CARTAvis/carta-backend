//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/ImageMoments.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_H_
#define CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_H_

#include <casacore/lattices/Lattices/MaskedLattice.h>
#include <casacore/scimath/Functionals/Gaussian1D.h>
#include <imageanalysis/ImageAnalysis/CasaImageBeamSet.h>
#include <imageanalysis/ImageAnalysis/Image2DConvolver.h>
#include <imageanalysis/ImageAnalysis/ImageHistograms.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgress.h>
#include <imageanalysis/ImageAnalysis/MomentClip.h>
#include <imageanalysis/ImageAnalysis/MomentFit.h>
#include <imageanalysis/ImageAnalysis/MomentWindow.h>
#include <imageanalysis/ImageAnalysis/MomentsBase.h>
#include <imageanalysis/ImageAnalysis/SepImageConvolver.h>

namespace carta {

template <class T>
class ImageMoments : public casa::MomentsBase<T> {
public:
    // Note that if I don't put MomentCalcBase as a forward declaration
    // and use instead  "friend class MomentCalcBase<T>"  The gnu compiler
    // fails with a typedef problem.  But I can't solve it with say
    // <src>typedef MomentCalcBase<T> gpp_type;</src>  because you can only do a
    // typedef with an actual type, not a <tt>T</tt> !
    // friend class MomentCalcBase<T>;

    ImageMoments() = delete;

    // Constructor takes an image and a <src>casacore::LogIO</src> object for
    // logging purposes. You specify whether output images are automatically
    // overwritten if pre-existing, or whether an intercative choice dialog widget
    // appears (overWriteOutput=F) You may also determine whether a progress meter
    // is displayed or not.
    ImageMoments(const casacore::ImageInterface<T>& image, casacore::LogIO& os, casacore::Bool overWriteOutput = false,
        casacore::Bool showProgress = true);

    ImageMoments(const ImageMoments<T>& other) = delete;

    // Destructor
    ~ImageMoments();

    ImageMoments<T>& operator=(const ImageMoments<T>& other) = delete;

    // Set the moment axis (0 relative).  A return value of <src>false</src>
    // indicates that the axis was not contained in the image. If you don't
    // call this function, the default state of the class is to set the
    // moment axis to the spectral axis if it can find one.  Otherwise
    // an error will result.
    casacore::Bool setMomentAxis(casacore::Int momentAxis);

    // This function invokes smoothing of the input image.  Give
    // <src>casacore::Int</src> arrays for the axes (0 relative) to be smoothed
    // and the smoothing kernel types (use the <src>enum KernelTypes</src>) for
    // each axis.  Give a <src>casacore::Double</src> array for the widths (full
    // width for BOXCAR and full width at half maximum for GAUSSIAN) in pixels of
    // the smoothing kernels for each axis.  For HANNING smoothing, you always get
    // the quarter-half-quarter kernel (no matter what you might ask for).  A
    // return value of <src>false</src> indicates that you have given an
    // inconsistent or invalid set of smoothing parameters.  If you don't call
    // this function the default state of the class is to do no smoothing.  The
    // kernel types are specified with the casacore::VectorKernel::KernelTypes
    // enum
    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxes, const casacore::Vector<casacore::Int>& kernelTypes,
        const casacore::Vector<casacore::Quantum<casacore::Double>>& kernelWidths);

    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxes, const casacore::Vector<casacore::Int>& kernelTypes,
        const casacore::Vector<casacore::Double>& kernelWidths);

    // This is the function that does all the computational work.  It should be
    // called after the <src>set</src> functions. If the axis being collapsed
    // comes from a coordinate with one axis only, the axis and its coordinate are
    // physically removed from the output image.  Otherwise, if
    // <src>removeAxes=true</src> then the output axis is logically removed from
    // the the output CoordinateSystem.  If <src>removeAxes=false</src> then the
    // axis is retained with shape=1 and with its original coordinate information
    // (which is probably meaningless).
    //
    // The output vector will hold PagedImages or TempImages (doTemp=true).
    // If doTemp is true, the outFileName is not used.
    //
    // If you create PagedImages, outFileName is the root name for
    // the output files.  Suffixes will be made up internally to append
    // to this root.  If you only ask for one moment,
    // this will be the actual name of the output file.  If you don't set this
    // variable, the default state of the class is to set the output name root to
    // the name of the input file.
    std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> createMoments(
        casacore::Bool doTemp, const casacore::String& outFileName, casacore::Bool removeAxes = true);

    // Set a new image.  A return value of <src>false</src> indicates the
    // image had an invalid type (this class only accepts casacore::Float or
    // casacore::Double images).
    casacore::Bool setNewImage(const casacore::ImageInterface<T>& image);

    // Get CoordinateSystem
    const casacore::CoordinateSystem& coordinates() {
        return _image->coordinates();
    };

    // Get shape
    casacore::IPosition getShape() const {
        return _image->shape();
    }

    // Set an ImageMomentsProgressMonitor interested in getting updates on the
    // progress of the collapse process.
    void setProgressMonitor(casa::ImageMomentsProgressMonitor* progressMonitor);

    // Stop the calculation
    void StopCalculation();

private:
    SPCIIT _image = SPCIIT(nullptr);
    casa::ImageMomentsProgressMonitor* _progressMonitor = nullptr;

    // casacore::Smooth an image
    SPIIT _smoothImage();

    // Determine the noise by fitting a Gaussian to a histogram
    // of the entire image above the 25% levels.  If a plotting
    // device is set, the user can interact with this process.
    void _whatIsTheNoise(T& noise, const casacore::ImageInterface<T>& image);

    // Iterate through a cube image with the moments calculator
    void lineMultiApply(PtrBlock<MaskedLattice<T>*>& latticeOut, const MaskedLattice<T>& latticeIn, LineCollapser<T, T>& collapser,
        uInt collapseAxis, LatticeProgress* tellProgress = 0);

    // Get a suitable chunk shape in order for the iteration
    casacore::IPosition _chunkShape(uInt axis, const MaskedLattice<T>& latticeIn);

    // Stop moment calculation
    volatile bool _stop;

protected:
    using casa::MomentsBase<T>::os_p;
    using casa::MomentsBase<T>::showProgress_p;
    using casa::MomentsBase<T>::momentAxisDefault_p;
    using casa::MomentsBase<T>::peakSNR_p;
    using casa::MomentsBase<T>::stdDeviation_p;
    using casa::MomentsBase<T>::yMin_p;
    using casa::MomentsBase<T>::yMax_p;
    using casa::MomentsBase<T>::out_p;
    using casa::MomentsBase<T>::smoothOut_p;
    using casa::MomentsBase<T>::goodParameterStatus_p;
    using casa::MomentsBase<T>::doWindow_p;
    using casa::MomentsBase<T>::doFit_p;
    using casa::MomentsBase<T>::doSmooth_p;
    using casa::MomentsBase<T>::noInclude_p;
    using casa::MomentsBase<T>::noExclude_p;
    using casa::MomentsBase<T>::fixedYLimits_p;
    using casa::MomentsBase<T>::momentAxis_p;
    using casa::MomentsBase<T>::worldMomentAxis_p;
    using casa::MomentsBase<T>::kernelTypes_p;
    using casa::MomentsBase<T>::kernelWidths_p;
    using casa::MomentsBase<T>::moments_p;
    using casa::MomentsBase<T>::selectRange_p;
    using casa::MomentsBase<T>::smoothAxes_p;
    using casa::MomentsBase<T>::overWriteOutput_p;
    using casa::MomentsBase<T>::error_p;
    using casa::MomentsBase<T>::convertToVelocity_p;
    using casa::MomentsBase<T>::velocityType_p;
    using casa::MomentsBase<T>::_checkMethod;
};

} // namespace carta

#include "ImageMoments.tcc"

#endif // CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_H_
