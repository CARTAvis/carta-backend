/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "casa/code/imageanalysis/ImageAnalysis/ImageMoments.h"
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
    ImageMoments(const casacore::ImageInterface<T>& image, casacore::LogIO& os, casacore::Bool over_write_output = false,
        casacore::Bool show_progress = true);

    ~ImageMoments(){};

    casacore::Bool setMomentAxis(const Int moment_axis);

    // This function invokes smoothing of the input image. Give casacore::Int arrays for the axes (0 relative) to be smoothed and the
    // smoothing kernel types (use the <src>enum KernelTypes</src>) for each axis. Give a casacore::Double array for the widths (full width
    // for BOXCAR and full width at half maximum for GAUSSIAN) in pixels of the smoothing kernels for each axis. For HANNING smoothing, you
    // always get the quarter-half-quarter kernel (no matter what you might ask for). A return value of false indicates that you have given
    // an inconsistent or invalid set of smoothing parameters. If you don't call this function the default state of the class is to do no
    // smoothing. The kernel types are specified with the casacore::VectorKernel::KernelTypes enum
    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smooth_axes, const casacore::Vector<casacore::Int>& kernel_types,
        const casacore::Vector<casacore::Quantum<casacore::Double>>& kernel_widths);

    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smooth_axes, const casacore::Vector<casacore::Int>& kernel_types,
        const casacore::Vector<casacore::Double>& kernel_widths_pix);

    // This is the function that does all the computational work. The output vector will hold PagedImages or TempImages (do_temp = true).
    // If do_temp is true, the out_file_name is not used. If you create PagedImages, out_file_name is the root name for the output files.
    // If you don't set this variable, the default state of the class is to set the output name root to the name of the input file.
    std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> createMoments(
        casacore::Bool do_temp, const casacore::String& out_file_name, casacore::Bool remove_axis = true);

    // Get coordinate system
    const casacore::CoordinateSystem& coordinates() {
        return _image->coordinates();
    };

    // Get image shape
    casacore::IPosition getShape() const {
        return _image->shape();
    }

    // Set an ImageMomentsProgressMonitor interested in getting updates on the progress of the collapse process
    void SetProgressMonitor(casa::ImageMomentsProgressMonitor* progressMonitor);

    // Stop the calculation
    void StopCalculation();

private:
    SPCIIT _image = SPCIIT(nullptr);
    casa::ImageMomentsProgressMonitor* _progress_monitor = nullptr;

    casacore::Bool SetNewImage(const casacore::ImageInterface<T>& image);

    // casacore::Smooth an image
    SPIIT SmoothImage();

    // Determine the noise by fitting a Gaussian to a histogram of the entire image above the 25% levels. If a plotting device is set, the
    // user can interact with this process.
    void WhatIsTheNoise(T& noise, const casacore::ImageInterface<T>& image);

    // Iterate through a cube image with the moments calculator. Re-write from the casacore::LatticeApply<T,U>::lineMultiApply() function
    void LineMultiApply(casacore::PtrBlock<casacore::MaskedLattice<T>*>& lattice_out, const casacore::MaskedLattice<T>& lattice_in,
        casacore::LineCollapser<T, T>& collapser, casacore::uInt collapse_axis, casacore::LatticeProgress* tell_progress = 0);

    // Get a suitable chunk shape in order for the iteration
    casacore::IPosition ChunkShape(casacore::uInt axis, const casacore::MaskedLattice<T>& lattice_in);

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
