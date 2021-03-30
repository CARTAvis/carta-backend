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

#include <imageanalysis/ImageAnalysis/ImageTask.h>

#include <casa/Arrays/Array.h>
#include <casa/Quanta/Quantum.h>
#include <casa/aips.h>
#include <scimath/Mathematics/VectorKernel.h>

namespace casacore {

template <class T>
class ImageInterface;
template <class T>
class Matrix;
template <class T>
class Vector;
class String;
class IPosition;
class CoordinateSystem;
class ImageInfo;
class Unit;
class GaussianBeam;

} // namespace casacore

namespace carta {

// <summary>
// This class does 2D convolution of an image by a functional form
// </summary>

// <use visibility=export>

// <reviewed reviewer="" date="yyyy/mm/dd" tests="" demos="">
// </reviewed>

// <prerequisite>
//   <li> <linkto class="casacore::ImageInterface">casacore::ImageInterface</linkto>
//   <li> <linkto class="casacore::Convolver">casacore::Convolver</linkto>
// </prerequisite>

// <etymology>
// This class handles 2D convolution of images
// </etymology>

// <synopsis>
// This class convolves an image by a specified 2D function.
// </synopsis>

// <example>
// <srcBlock>
// </srcBlock>
// </example>

// <motivation>
// Convolution is a standard image processing requirement.  The
// class object has no state.
// The convolution is done via FFT.  Thus input pixels which
// are masked are set to 0 before the convolution.  The mask
// is transferred to the output image.  No additional scaling
// of the output image values is done.
//
// </motivation>

// <todo asof="2001/08/28">
//   <li>
// </todo>

template <class T>
class Image2DConvolver : public casa::ImageTask<T> {
public:
    const static casacore::String CLASS_NAME;

    Image2DConvolver() = delete;

    Image2DConvolver(const SPCIIT image, const casacore::Record* const& regionPtr, const casacore::String& mask,
        const casacore::String& outname, const casacore::Bool overwrite);

    Image2DConvolver(const Image2DConvolver<T>& other) = delete;

    ~Image2DConvolver() {}

    Image2DConvolver& operator=(const Image2DConvolver<T>& other) = delete;

    SPIIT convolve();

    // type is a string that starts with "g" (gaussian), "b" (boxcar), or "h"
    // (hanning), and is case insensitive
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

    // if true, do not log certain info/warning messages which would normally
    // be logged during convolution
    void setSuppressWarnings(casacore::Bool b) {
        _suppressWarnings = b;
    }

    void StopCalculation(); // cancel calculations

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
    casacore::Bool _suppressWarnings = casacore::False;
    volatile bool _stop; // used for cancellation

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

    // The kernel is currently always real-valued, so make it Double at this
    // point to avoid unnecessary templating issues if the image has is
    // complex valued
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

    void _log(const String& msg, LogIO::Command priority) const;
};

} // namespace carta

#include "Image2DConvolver.tcc"

#endif // CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_
