/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/ImageMoments.h"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGEMOMENTS_H_
#define CARTA_BACKEND__MOMENT_IMAGEMOMENTS_H_

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

#include "Image2DConvolver.h"

namespace carta {

template <class T>
class ImageMoments : public casa::MomentsBase<T> {
public:
    ImageMoments(const casacore::ImageInterface<T>& image, casacore::LogIO& os, casa::ImageMomentsProgressMonitor* progress_monitor);
    ~ImageMoments() = default;

    casacore::Bool setMomentAxis(const Int moment_axis);
    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smooth_axes, const casacore::Vector<casacore::Int>& kernel_types,
        const casacore::Vector<casacore::Quantum<casacore::Double>>& kernel_widths) {
        return false;
    }
    std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> createMoments(
        const casacore::String& out_file_name, casacore::Bool remove_axis = true);

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

private:
    SPCIIT _image;
    std::unique_ptr<casa::ImageMomentsProgress> _progress_monitor;
    std::unique_ptr<carta::Image2DConvolver<casacore::Float>> _image_2d_convolver;

    // Iterate through a cube image with the moments calculator. Re-write from the casacore::LatticeApply<T,U>::lineMultiApply() function
    void LineMultiApply(casacore::PtrBlock<casacore::MaskedLattice<T>*>& lattice_out, const casacore::MaskedLattice<T>& lattice_in,
        casacore::LineCollapser<T, T>& collapser, casacore::uInt collapse_axis);

    // Get a suitable chunk shape in order for the iteration
    casacore::IPosition ChunkShape(casacore::uInt axis, const casacore::MaskedLattice<T>& lattice_in);

    // Stop moment calculation
    volatile bool _stop;

    // Number of steps have done for the beam convolution
    casacore::uInt _steps_for_beam_convolution = 0;

protected:
    using casa::MomentsBase<T>::os_p;
    using casa::MomentsBase<T>::momentAxis_p;
    using casa::MomentsBase<T>::worldMomentAxis_p;
    using casa::MomentsBase<T>::moments_p;
    using casa::MomentsBase<T>::convertToVelocity_p;
};

} // namespace carta

#include "ImageMoments.tcc"

#endif // CARTA_BACKEND__MOMENT_IMAGEMOMENTS_H_
