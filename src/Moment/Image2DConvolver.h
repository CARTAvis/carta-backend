/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/Image2DConvolver.h"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_
#define CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_

#include <casa/Arrays/Array.h>
#include <casa/Arrays/ArrayMath.h>
#include <casa/Arrays/IPosition.h>
#include <casa/Arrays/Matrix.h>
#include <casa/Arrays/Vector.h>
#include <casa/Exceptions/Error.h>
#include <casa/Logging/LogIO.h>
#include <casa/Quanta/MVAngle.h>
#include <casa/Quanta/QLogical.h>
#include <casa/Quanta/Quantum.h>
#include <casa/Quanta/Unit.h>
#include <casa/aips.h>
#include <casa/iostream.h>
#include <components/ComponentModels/GaussianDeconvolver.h>
#include <components/ComponentModels/GaussianShape.h>
#include <components/ComponentModels/SkyComponentFactory.h>
#include <coordinates/Coordinates/CoordinateSystem.h>
#include <coordinates/Coordinates/CoordinateUtil.h>
#include <coordinates/Coordinates/DirectionCoordinate.h>
#include <imageanalysis/ImageAnalysis/ImageConvolver.h>
#include <imageanalysis/ImageAnalysis/ImageMetaData.h>
#include <imageanalysis/ImageAnalysis/ImageTask.h>
#include <images/Images/ImageInfo.h>
#include <images/Images/ImageInterface.h>
#include <images/Images/ImageUtilities.h>
#include <images/Images/PagedImage.h>
#include <images/Images/SubImage.h>
#include <images/Images/TempImage.h>
#include <lattices/LatticeMath/Fit2D.h>
#include <scimath/Functionals/Gaussian2D.h>
#include <scimath/Mathematics/Convolver.h>
#include <scimath/Mathematics/VectorKernel.h>

#include <memory>

namespace carta {

template <class T>
class Image2DConvolver {
public:
    Image2DConvolver(const SPCIIT image, const std::pair<casacore::uInt, casacore::uInt>& axes, const casacore::GaussianBeam& max_beam,
        casa::ImageMomentsProgress* progress_monitor);

    ~Image2DConvolver() = default;

    SPIIT DoConvolve();
    void StopCalculation();
    casacore::uInt GetTotalSteps() {
        return _total_steps;
    }

private:
    casacore::Quantity _major, _minor, _pa;
    casacore::IPosition _axes;
    volatile bool _stop;                           // used for cancellation
    casa::ImageMomentsProgress* _progress_monitor; // used to report the progress
    mutable casacore::uInt _total_steps = 0;       // total number of steps for the beam convolution
    std::shared_ptr<const casacore::ImageInterface<T>> _image;
    mutable std::shared_ptr<casacore::LogIO> _log = std::shared_ptr<casacore::LogIO>(new casacore::LogIO());

    void Convolve(SPIIT image_out, const casacore::ImageInterface<T>& image_in) const;

    void SetKernel(const casacore::Quantity& major, const casacore::Quantity& minor, const casacore::Quantity& pa);
    void SetAxes(const std::pair<casacore::uInt, casacore::uInt>& axes);

    // returns the value by which pixel values will be scaled
    casacore::Double DealWithRestoringBeam(casacore::String& brightness_unit_out, casacore::GaussianBeam& beam_out,
        const casacore::Array<casacore::Double>& kernel_array, casacore::Double kernel_volume,
        const casacore::Vector<casacore::Quantity>& parameters, const casacore::CoordinateSystem& csys,
        const casacore::GaussianBeam& beam_in, const casacore::Unit& brightness_unit_in) const;

    void DoMultipleBeams(casacore::ImageInfo& image_info_out, casacore::Double& kernel_volume, SPIIT image_out,
        casacore::String& brightness_unit_out, casacore::GaussianBeam& beam_out, casacore::Double factor1,
        const casacore::ImageInterface<T>& image_in, const std::vector<casacore::Quantity>& original_params,
        std::vector<casacore::Quantity>& kernel_params, casacore::Array<casacore::Double>& kernel, casacore::Bool log_factors,
        casacore::Double pixel_area) const;

    // The kernel is currently always real-valued, so make it Double at this point to avoid unnecessary templating issues if the image has
    // is complex valued
    void DoSingleBeam(casacore::ImageInfo& image_info_out, casacore::Double& kernel_volume, std::vector<Quantity>& kernel_params,
        casacore::Array<casacore::Double>& kernel, casacore::String& brightness_unit_out, casacore::GaussianBeam& beam_out, SPIIT image_out,
        const casacore::ImageInterface<T>& image_in, const std::vector<Quantity>& original_params, casacore::Bool log_factors,
        casacore::Double factor1, casacore::Double pixel_area) const;

    casacore::Double FillKernel(casacore::Matrix<casacore::Double>& kernel_matrix, const casacore::IPosition& kernel_shape,
        const casacore::Vector<casacore::Double>& parameters) const;

    void FillGaussian(casacore::Double& max_val, casacore::Double& volume, casacore::Matrix<casacore::Double>& pixels,
        casacore::Double height, casacore::Double x_centre, casacore::Double y_centre, casacore::Double major_axis, casacore::Double ratio,
        casacore::Double position_angle) const;

    casacore::Double MakeKernel(casacore::Array<casacore::Double>& kernel_array, const std::vector<casacore::Quantity>& parameters,
        const casacore::ImageInterface<T>& image_in) const;

    casacore::IPosition ShapeOfKernel(const casacore::Vector<casacore::Double>& parameters, const casacore::uInt ndim) const;

    casacore::uInt SizeOfGaussian(const casacore::Double width, const casacore::Double nsigma) const;

    std::vector<casacore::Quantity> GetConvolvingBeamForTargetResolution(
        const std::vector<casacore::Quantity>& target_beam_params, const casacore::GaussianBeam& input_beam) const;

    void LogBeamInfo(const casacore::ImageInfo& image_info, const casacore::String& description) const;

    SPIIT PrepareOutputImage(const casacore::ImageInterface<T>& image) const;

    static void CopyMask(casacore::Lattice<casacore::Bool>& mask, const casacore::ImageInterface<T>& image);
};

} // namespace carta

#include "Image2DConvolver.tcc"

#endif // CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_
