/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/ImageMoments.h"
//
#ifndef CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_H_
#define CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_H_

#include <casacore/lattices/Lattices/MaskedLattice.h>
#include <casacore/scimath/Functionals/Gaussian1D.h>
#include <imageanalysis/ImageAnalysis/CasaImageBeamSet.h>
#include <imageanalysis/ImageAnalysis/ImageHistograms.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgress.h>
#include <imageanalysis/ImageAnalysis/MomentClip.h>
#include <imageanalysis/ImageAnalysis/MomentFit.h>
#include <imageanalysis/ImageAnalysis/MomentWindow.h>
#include <imageanalysis/ImageAnalysis/MomentsBase.h>
#include <imageanalysis/ImageAnalysis/SepImageConvolver.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "Image2DConvolver.h"
#include "SlabPlan.h"

namespace carta {

template <class T>
class ImageMoments : public casa::MomentsBase<T> {
public:
    ImageMoments(const casacore::ImageInterface<T>& image, casacore::LogIO& os, casa::ImageMomentsProgressMonitor* progress_monitor,
        casacore::Bool over_write_output = false);

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

    // Stop the calculation
    void StopCalculation();

    // The chunk grid of the image this was made from: the shape of a chunk, and where this image's
    // corner lies among them, both in this image's axis order. With it the moment shapes its slabs to
    // the chunks themselves rather than to the image's cursor advice, and counts what they cost knowing
    // where the chunks lie (see SlabPlan). Kept only while the image collapsed is that image: one
    // convolved from it is held in memory, and decodes in no chunks.
    //
    // `hold_cache`, when given, holds a cache of decoded chunks for the walk's reads to keep what they
    // decode in: called with a size once the slabs are planned, it returns what keeps a cache that large,
    // which the walk lets go of when it is done. The size is what neighbouring slabs share, so that each
    // chunk is decoded once (see SlabPlan::CacheBytes).
    //
    // `decoded_chunk_bytes` is what one chunk keeps in that cache, as the image says: its stored type
    // and the flag chunks beside it, which is what the cache is counted in. Zero when the image does not
    // say, and a chunk is then counted as its pixels in T.
    void SetChunkGrid(const casacore::IPosition& unit, const casacore::IPosition& origin,
        std::function<std::shared_ptr<void>(std::uint64_t bytes)> hold_cache = {}, std::uint64_t decoded_chunk_bytes = 0) {
        _chunk_grid_unit = unit;
        _chunk_grid_origin = origin;
        _hold_cache = std::move(hold_cache);
        _decoded_chunk_bytes = decoded_chunk_bytes;
    }

private:
    SPCIIT _image = SPCIIT(nullptr);
    std::unique_ptr<casa::ImageMomentsProgress> _progress_monitor;
    std::unique_ptr<Image2DConvolver<casacore::Float>> _image_2d_convolver;

    casacore::Bool SetNewImage(const casacore::ImageInterface<T>& image);

    // casacore::Smooth an image
    SPIIT SmoothImage();

    // Determine the noise by fitting a Gaussian to a histogram of the entire image above the 25% levels. If a plotting device is set, the
    // user can interact with this process.
    void WhatIsTheNoise(T& noise, const casacore::ImageInterface<T>& image);

    // Iterate through a cube image with the moments calculator. Re-write from the casacore::LatticeApply<T,U>::lineMultiApply() function
    // One collapser per worker. They hold their own scratch -- selectedData_p, pixelIn_p, a whole
    // copy of the coordinate system -- so a shared one cannot be called from more than one thread,
    // and the walk is 77-86% of the time inside that call.
    void LineMultiApply(casacore::PtrBlock<casacore::MaskedLattice<T>*>& lattice_out, const casacore::MaskedLattice<T>& lattice_in,
        const std::vector<std::shared_ptr<casa::MomentCalcBase<T>>>& collapsers, casacore::uInt collapse_axis);

    // Stop moment calculation. Set by StopCalculation on another thread, and read by every worker of
    // the parallel walk as well as by the thread that runs it, so it is atomic: volatile made each
    // read happen, but not the reads and the write race-free.
    std::atomic<bool> _stop;

    // Number of steps have done for the beam convolution
    casacore::uInt _steps_for_beam_convolution = 0;

    // See SetChunkGrid. Empty when not known.
    casacore::IPosition _chunk_grid_unit;
    casacore::IPosition _chunk_grid_origin;
    std::function<std::shared_ptr<void>(std::uint64_t bytes)> _hold_cache;
    std::uint64_t _decoded_chunk_bytes = 0;

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

#endif // CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_H_
