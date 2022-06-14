/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
class Image2DConvolver : public casa::ImageTask<T> {
public:
    const static casacore::String CLASS_NAME;

    Image2DConvolver() = delete;

    Image2DConvolver(const SPCIIT image, const casacore::Record* const& regionPtr, const casacore::String& mask,
        const casacore::String& outname, const casacore::Bool overwrite, casa::ImageMomentsProgress* progress_monitor);
    Image2DConvolver(const Image2DConvolver<T>& other) = delete;

    ~Image2DConvolver() {}

    Image2DConvolver& operator=(const Image2DConvolver<T>& other) = delete;

    SPIIT convolve();

    // type is a string that starts with "g" (gaussian), "b" (boxcar), or "h" (hanning), and is case insensitive
    void setKernel(
        const casacore::String& type, const casacore::Quantity& major, const casacore::Quantity& minor, const casacore::Quantity& pa);
    void setScale(casacore::Double d) {
        _scale = d;
    }
    void setAxes(const std::pair<casacore::uInt, casacore::uInt>& axes);
    void setTargetRes(casacore::Bool b) {
        _targetres = b;
    }

    casacore::String getClass() const {
        return CLASS_NAME;
    }

    void StopCalculation();

    casacore::uInt GetTotalSteps() {
        return _total_steps;
    }

protected:
    casa::CasacRegionManager::StokesControl _getStokesControl() const {
        return casa::CasacRegionManager::USE_ALL_STOKES;
    }
    std::vector<casacore::Coordinate::Type> _getNecessaryCoordinates() const {
        return std::vector<casacore::Coordinate::Type>();
    }
    inline casacore::Bool _supportsMultipleRegions() const {
        return true;
    }

private:
    casacore::VectorKernel::KernelTypes _type;
    casacore::Double _scale;
    casacore::Quantity _major, _minor, _pa;
    casacore::IPosition _axes;
    casacore::Bool _targetres = casacore::False;
    volatile bool _stop;                           // used for cancellation
    casa::ImageMomentsProgress* _progress_monitor; // used to report the progress
    mutable casacore::uInt _total_steps = 0;       // total number of steps for the beam convolution

    void _checkKernelParameters(
        casacore::VectorKernel::KernelTypes kernelType, const casacore::Vector<casacore::Quantity>& parameters) const;

    void _convolve(SPIIT imageOut, const casacore::ImageInterface<T>& imageIn, casacore::VectorKernel::KernelTypes kernelType) const;

    // returns the value by which pixel values will be scaled
    Double _dealWithRestoringBeam(casacore::String& brightnessUnitOut, casacore::GaussianBeam& beamOut,
        const casacore::Array<Double>& kernelArray, Double kernelVolume, const casacore::VectorKernel::KernelTypes kernelType,
        const casacore::Vector<casacore::Quantity>& parameters, const casacore::CoordinateSystem& cSys,
        const casacore::GaussianBeam& beamIn, const casacore::Unit& brightnessUnit, casacore::Bool emitMessage) const;

    void _doMultipleBeams(ImageInfo& iiOut, Double& kernelVolume, SPIIT imageOut, String& brightnessUnitOut, GaussianBeam& beamOut,
        Double factor1, const ImageInterface<T>& imageIn, const std::vector<Quantity>& originalParms, std::vector<Quantity>& kernelParms,
        Array<Double>& kernel, VectorKernel::KernelTypes kernelType, Bool logFactors, Double pixelArea) const;

    // The kernel is currently always real-valued, so make it Double at this point to avoid unnecessary templating issues if the image has
    // is complex valued
    void _doSingleBeam(ImageInfo& iiOut, Double& kernelVolume, std::vector<Quantity>& kernelParms, Array<Double>& kernel,
        String& brightnessUnitOut, GaussianBeam& beamOut, SPIIT imageOut, const ImageInterface<T>& imageIn,
        const std::vector<Quantity>& originalParms, VectorKernel::KernelTypes kernelType, Bool logFactors, Double factor1,
        Double pixelArea) const;

    Double _fillKernel(casacore::Matrix<Double>& kernelMatrix, casacore::VectorKernel::KernelTypes kernelType,
        const casacore::IPosition& kernelShape, const casacore::Vector<casacore::Double>& parameters) const;

    void _fillGaussian(Double& maxVal, Double& volume, casacore::Matrix<Double>& pixels, Double height, Double xCentre, Double yCentre,
        Double majorAxis, Double ratio, Double positionAngle) const;

    Double _makeKernel(casacore::Array<Double>& kernel, casacore::VectorKernel::KernelTypes kernelType,
        const std::vector<casacore::Quantity>& parameters, const casacore::ImageInterface<T>& inImage) const;

    casacore::IPosition _shapeOfKernel(const casacore::VectorKernel::KernelTypes kernelType,
        const casacore::Vector<casacore::Double>& parameters, const casacore::uInt ndim) const;

    casacore::uInt _sizeOfGaussian(const casacore::Double width, const casacore::Double nSigma) const;

    std::vector<casacore::Quantity> _getConvolvingBeamForTargetResolution(
        const std::vector<casacore::Quantity>& targetBeamParms, const casacore::GaussianBeam& inputBeam) const;

    void _logBeamInfo(const ImageInfo& imageInfo, const String& desc) const;
};

} // namespace carta

#include "Image2DConvolver.tcc"

#endif // CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_
