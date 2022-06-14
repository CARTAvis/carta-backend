/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/Image2DConvolver.tcc"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_TCC_
#define CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_TCC_

#include "../Logger/Logger.h"
#include "Util/Casacore.h"

using namespace carta;

template <class T>
const casacore::String Image2DConvolver<T>::CLASS_NAME = "Image2DConvolver";

template <class T>
Image2DConvolver<T>::Image2DConvolver(const SPCIIT image, const casacore::Record* const& region, const casacore::String& mask,
    const casacore::String& outname, const casacore::Bool overwrite, casa::ImageMomentsProgress* progress_monitor)
    : casa::ImageTask<T>(image, "", region, "", "", "", mask, outname, overwrite),
      _type(casacore::VectorKernel::GAUSSIAN),
      _scale(0),
      _major(),
      _minor(),
      _pa(),
      _axes(image->coordinates().directionAxesNumbers()),
      _stop(false),
      _progress_monitor(progress_monitor) {
    this->_construct(true);
}

// TODO use GaussianBeams rather than casacore::Vector<casacore::Quantity>s, this method can probably be eliminated.
template <class T>
std::vector<casacore::Quantity> Image2DConvolver<T>::_getConvolvingBeamForTargetResolution(
    const std::vector<casacore::Quantity>& targetBeamParms, const casacore::GaussianBeam& inputBeam) const {
    casacore::GaussianBeam convolvingBeam;
    casacore::GaussianBeam targetBeam(targetBeamParms[0], targetBeamParms[1], targetBeamParms[2]);
    try {
        if (casa::GaussianDeconvolver::deconvolve(convolvingBeam, targetBeam, inputBeam)) {
            // point source, or convolvingBeam nonsensical
            throw casacore::AipsError();
        }
    } catch (const casacore::AipsError& x) {
        string error = fmt::format(
            "Unable to reach target resolution of {}. Input image beam {} is (nearly) identical to or larger than the output beam size.",
            FormatBeam(targetBeam), FormatBeam(inputBeam));
        spdlog::error(error);
        ThrowCc(error);
    }
    std::vector<casacore::Quantity> kernelParms{convolvingBeam.getMajor(), convolvingBeam.getMinor(), convolvingBeam.getPA(true)};

    return kernelParms;
}

template <class T>
void Image2DConvolver<T>::setAxes(const std::pair<casacore::uInt, casacore::uInt>& axes) {
    casacore::uInt ndim = this->_getImage()->ndim();
    ThrowIf(axes.first == axes.second, "Axes must be different");
    ThrowIf(axes.first >= ndim || axes.second >= ndim, "Axis value must be less than number of axes in image");
    if (_axes.size() != 2) {
        _axes.resize(2, false);
    }
    _axes[0] = axes.first;
    _axes[1] = axes.second;
}

template <class T>
void Image2DConvolver<T>::setKernel(
    const casacore::String& type, const casacore::Quantity& major, const casacore::Quantity& minor, const casacore::Quantity& pa) {
    ThrowIf(major < minor, "Major axis is less than minor axis");
    _type = casacore::VectorKernel::toKernelType(type);
    _major = major;
    _minor = minor;
    _pa = pa;
}

template <class T>
SPIIT Image2DConvolver<T>::convolve() {
    _stop = false; // reset the flag for cancellation

    ThrowIf(_axes.nelements() != 2, "You must give two pixel axes to convolve");

    auto inc = this->_getImage()->coordinates().increment();
    auto units = this->_getImage()->coordinates().worldAxisUnits();
    ThrowIf(!near(casacore::Quantity(fabs(inc[_axes[0]]), units[_axes[0]]), casacore::Quantity(fabs(inc[_axes[1]]), units[_axes[1]])),
        "Pixels must be square, please repair your image so that they are");

    auto subImage = casa::SubImageFactory<T>::createImage(
        *this->_getImage(), "", *this->_getRegion(), this->_getMask(), this->_getDropDegen(), false, false, this->_getStretch());
    const casacore::Int nDim = subImage->ndim();
    ThrowIf(_axes(0) < 0 || _axes(0) >= nDim || _axes(1) < 0 || _axes(1) >= nDim,
        "The pixel axes " + casacore::String::toString(_axes) + " are illegal");
    ThrowIf(nDim < 2, "The image axes must have at least 2 pixel axes");

    shared_ptr<TempImage<T>> outImage(new casacore::TempImage<T>(subImage->shape(), subImage->coordinates()));
    _convolve(outImage, *subImage, _type);

    if (subImage->isMasked()) {
        TempLattice<Bool> mask(outImage->shape());
        casa::ImageTask<T>::_copyMask(mask, *subImage);
        outImage->attachMask(mask);
    }

    return this->_prepareOutputImage(*outImage);
}

template <class T>
void Image2DConvolver<T>::_convolve(SPIIT imageOut, const ImageInterface<T>& imageIn, VectorKernel::KernelTypes kernelType) const {
    const auto& inShape = imageIn.shape();
    const auto& outShape = imageOut->shape();
    ThrowIf(!inShape.isEqual(outShape), "Input and output images must have the same shape");

    // Generate Kernel Array (height unity)
    ThrowIf(_targetres && kernelType != casacore::VectorKernel::GAUSSIAN, "targetres can only be true for a Gaussian convolving kernel");

    // maybe can remove this comment if I'm smart enough kernel needs to be type T because ultimately we use ImageConvolver which requires
    // the kernel and input image to be of the same type. This is kind of stupid because our kernels are always real-valued, and we use
    // Fit2D which requires a real-valued kernel, so it seems we could support complex valued images and real valued kernels if
    // ImageConvolver was smarter
    Array<Double> kernel;

    // initialize to avoid compiler warning, kernelVolume will always be set to something reasonable below before it is used.
    Double kernelVolume = -1;
    std::vector<casacore::Quantity> originalParms{_major, _minor, _pa};

    if (!_targetres) {
        kernelVolume = _makeKernel(kernel, kernelType, originalParms, imageIn);
    }

    const auto& cSys = imageIn.coordinates();
    if (_major.getUnit().startsWith("pix")) {
        auto inc = cSys.increment()[_axes[0]];
        auto unit = cSys.worldAxisUnits()[_axes[0]];
        originalParms[0] = _major.getValue() * casacore::Quantity(abs(inc), unit);
    }

    if (_minor.getUnit().startsWith("pix")) {
        auto inc = cSys.increment()[_axes[1]];
        auto unit = cSys.worldAxisUnits()[_axes[1]];
        originalParms[1] = _minor.getValue() * casacore::Quantity(abs(inc), unit);
    }

    auto kernelParms = originalParms;

    // Figure out output image restoring beam (if any), output units and scale factor for convolution kernel array
    GaussianBeam beamOut;
    const auto& imageInfo = imageIn.imageInfo();
    const auto& brightnessUnit = imageIn.units();

    String brightnessUnitOut;
    auto iiOut = imageOut->imageInfo();
    auto logFactors = false;
    Double factor1 = -1;
    double pixelArea = 0;

    auto autoScale = _scale <= 0;
    if (autoScale) {
        auto bunitUp = brightnessUnit.getName();
        bunitUp.upcase();
        logFactors = bunitUp.contains("/BEAM");
        if (logFactors) {
            pixelArea = cSys.directionCoordinate().getPixelArea().getValue("arcsec*arcsec");
            if (!_targetres) {
                GaussianBeam kernelBeam(kernelParms);
                factor1 = pixelArea / kernelBeam.getArea("arcsec*arcsec");
            }
        }
    }

    if (imageInfo.hasMultipleBeams()) {
        _doMultipleBeams(iiOut, kernelVolume, imageOut, brightnessUnitOut, beamOut, factor1, imageIn, originalParms, kernelParms, kernel,
            kernelType, logFactors, pixelArea);
    } else {
        _doSingleBeam(iiOut, kernelVolume, kernelParms, kernel, brightnessUnitOut, beamOut, imageOut, imageIn, originalParms, kernelType,
            logFactors, factor1, pixelArea);
    }

    imageOut->setUnits(brightnessUnitOut);
    imageOut->setImageInfo(iiOut);

    _logBeamInfo(imageInfo, "Original " + this->_getImage()->name());
    _logBeamInfo(iiOut, "Output " + this->_getOutname());
}

template <class T>
void Image2DConvolver<T>::_logBeamInfo(const ImageInfo& imageInfo, const String& desc) const {
    const auto& beamSet = imageInfo.getBeamSet();
    string message = desc;

    if (!imageInfo.hasBeam()) {
        message += " has no beam";
    } else if (imageInfo.hasSingleBeam()) {
        message += fmt::format(" resolution {} ", FormatBeam(beamSet.getBeam()));
    } else {
        message += fmt::format(" has multiple beams. Min area beam: {}. Max area beam: {}. Median area beam {}",
            FormatBeam(beamSet.getMinAreaBeam()), FormatBeam(beamSet.getMaxAreaBeam()), FormatBeam(beamSet.getMedianAreaBeam()));
    }
    spdlog::debug(message);
}

template <class T>
void Image2DConvolver<T>::_doSingleBeam(ImageInfo& iiOut, Double& kernelVolume, vector<Quantity>& kernelParms, Array<Double>& kernel,
    String& brightnessUnitOut, GaussianBeam& beamOut, SPIIT imageOut, const ImageInterface<T>& imageIn,
    const vector<Quantity>& originalParms, VectorKernel::KernelTypes kernelType, Bool logFactors, Double factor1, Double pixelArea) const {
    GaussianBeam inputBeam = imageIn.imageInfo().restoringBeam();

    if (_targetres) {
        kernelParms = _getConvolvingBeamForTargetResolution(originalParms, inputBeam);
        spdlog::debug("Convolving image that has a beam of {} with a Gaussian of {} to reach a target resolution of {}",
            FormatBeam(inputBeam), FormatBeam(GaussianBeam(kernelParms)), FormatBeam(GaussianBeam(originalParms)));

        kernelVolume = _makeKernel(kernel, kernelType, kernelParms, imageIn);
    }

    const CoordinateSystem& cSys = imageIn.coordinates();
    auto scaleFactor = _dealWithRestoringBeam(
        brightnessUnitOut, beamOut, kernel, kernelVolume, kernelType, kernelParms, cSys, inputBeam, imageIn.units(), true);
    string message;

    message += "Scaling pixel values by ";

    if (logFactors) {
        if (_targetres) {
            GaussianBeam kernelBeam(kernelParms);
            factor1 = pixelArea / kernelBeam.getArea("arcsec*arcsec");
        }
        Double factor2 = beamOut.getArea("arcsec*arcsec") / inputBeam.getArea("arcsec*arcsec");
        message += fmt::format(
            "inverse of area of convolution kernel in pixels ({:.6f}) times the ratio of the beam areas ({:.6f}) = ", factor1, factor2);
    }

    message += fmt::format("{:.6f}", scaleFactor);
    spdlog::debug(message);

    if (_targetres && near(beamOut.getMajor(), beamOut.getMinor(), 1e-7)) {
        // circular beam should have same PA as given by user if targetres
        beamOut.setPA(originalParms[2]);
    }

    // Convolve. We have already scaled the convolution kernel (with some trickery cleverer than what ImageConvolver can do) so no more
    // scaling
    casa::ImageConvolver<T> aic;
    Array<T> modKernel(kernel.shape());
    casacore::convertArray(modKernel, scaleFactor * kernel);
    aic.convolve(*this->_getLog(), *imageOut, imageIn, modKernel, casa::ImageConvolver<T>::NONE, 1.0, true);

    // Overwrite some bits and pieces in the output image to do with the restoring beam  and image units
    casacore::Bool holdsOneSkyAxis;
    casacore::Bool hasSky = CoordinateUtil::holdsSky(holdsOneSkyAxis, cSys, _axes.asVector());

    if (hasSky && !beamOut.isNull()) {
        iiOut.setRestoringBeam(beamOut);
    } else {
        // If one of the axes is in the sky plane, we must delete the restoring beam as it is no longer meaningful
        if (holdsOneSkyAxis) {
            spdlog::warn("Because you convolved just one of the sky axes, the output image does not have a valid spatial restoring beam.");
            iiOut.removeRestoringBeam();
        }
    }
}

template <class T>
void Image2DConvolver<T>::_doMultipleBeams(ImageInfo& iiOut, Double& kernelVolume, SPIIT imageOut, String& brightnessUnitOut,
    GaussianBeam& beamOut, Double factor1, const ImageInterface<T>& imageIn, const vector<Quantity>& originalParms,
    vector<Quantity>& kernelParms, Array<Double>& kernel, VectorKernel::KernelTypes kernelType, Bool logFactors, Double pixelArea) const {
    casa::ImageMetaData<T> md(imageOut);
    auto nChan = md.nChannels();
    auto nPol = md.nStokes();

    // initialize all beams to be null
    iiOut.setAllBeams(nChan, nPol, casacore::GaussianBeam());
    const auto& cSys = imageIn.coordinates();
    auto specAxis = cSys.spectralAxisNumber();
    auto polAxis = cSys.polarizationAxisNumber();

    casacore::IPosition start(imageIn.ndim(), 0);
    auto end = imageIn.shape();

    if (nChan > 0) {
        end[specAxis] = 1;
    }
    if (nPol > 0) {
        end[polAxis] = 1;
    }

    casacore::Int channel = -1;
    casacore::Int polarization = -1;

    if (_targetres) {
        iiOut.removeRestoringBeam();
        iiOut.setRestoringBeam(casacore::GaussianBeam(kernelParms));
    }

    casacore::uInt count = (nChan > 0 && nPol > 0) ? nChan * nPol : nChan > 0 ? nChan : nPol;
    if (_progress_monitor) {
        _progress_monitor->init(count * 2); // roughly estimate the total number of steps for the whole moments calculation is twice as the
                                            // number of steps for the beam convolution
        _total_steps = count;               // set total number of steps for the beam convolution
    }

    for (casacore::uInt i = 0; i < count; ++i) {
        if (_stop) { // cancel calculations
            break;
        }

        if (_progress_monitor) {
            _progress_monitor->nstepsDone(i);
        }

        if (nChan > 0) {
            channel = i % nChan;
            start[specAxis] = channel;
        }
        if (nPol > 0) {
            polarization = nChan > 1 ? (i - channel) % nChan : i;
            start[polAxis] = polarization;
        }
        casacore::Slicer slice(start, end);
        casacore::SubImage<T> subImage(imageIn, slice);
        casacore::CoordinateSystem subCsys = subImage.coordinates();

        if (subCsys.hasSpectralAxis()) {
            auto subRefPix = subCsys.referencePixel();
            subRefPix[specAxis] = 0;
            subCsys.setReferencePixel(subRefPix);
        }

        auto inputBeam = imageIn.imageInfo().restoringBeam(channel, polarization);
        auto doConvolve = true;

        if (_targetres) {
            string message;

            if (channel >= 0) {
                message += fmt::format("Channel {} of {}", channel, nChan);
                if (polarization >= 0) {
                    message += ", ";
                }
            }
            if (polarization >= 0) {
                message += fmt::format("Polarization {} of {}", polarization, nPol);
            }

            message += " ";

            if (near(inputBeam, GaussianBeam(originalParms), 1e-5, casacore::Quantity(1e-2, "arcsec"))) {
                doConvolve = false;
                message += fmt::format("Input beam is already near target resolution so this plane will not be convolved.");
            } else {
                kernelParms = _getConvolvingBeamForTargetResolution(originalParms, inputBeam);
                kernelVolume = _makeKernel(kernel, kernelType, kernelParms, imageIn);
                message += fmt::format(": Convolving image which has a beam of {} with a Gaussian of {} to reach a target resolution of {}",
                    FormatBeam(inputBeam), FormatBeam(GaussianBeam(kernelParms)), FormatBeam(GaussianBeam(originalParms)));
            }

            spdlog::debug(message);
        }

        casacore::TempImage<T> subImageOut(subImage.shape(), subImage.coordinates());
        if (doConvolve) {
            auto scaleFactor = _dealWithRestoringBeam(
                brightnessUnitOut, beamOut, kernel, kernelVolume, kernelType, kernelParms, subCsys, inputBeam, imageIn.units(), i == 0);
            {
                string message("Scaling pixel values by ");
                if (logFactors) {
                    if (_targetres) {
                        casacore::GaussianBeam kernelBeam(kernelParms);
                        factor1 = pixelArea / kernelBeam.getArea("arcsec*arcsec");
                    }
                    auto factor2 = beamOut.getArea("arcsec*arcsec") / inputBeam.getArea("arcsec*arcsec");
                    message += fmt::format(
                        "inverse of area of convolution kernel in pixels ({:.6f})  times the ratio of the beam areas ({:.6f}) = ", factor1,
                        factor2);
                }
                message += " for ";
                if (channel >= 0) {
                    message += "channel number ";
                    if (polarization >= 0) {
                        message += " and ";
                    }
                }
                if (polarization >= 0) {
                    message += "polarization number ";
                }
                spdlog::debug(message);
            }

            if (_targetres && near(beamOut.getMajor(), beamOut.getMinor(), 1e-7)) {
                // circular beam should have same PA as given by user if targetres
                beamOut.setPA(originalParms[2]);
            }

            Array<T> modKernel(kernel.shape());
            casacore::convertArray(modKernel, scaleFactor * kernel);
            casa::ImageConvolver<T> aic;
            aic.convolve(*this->_getLog(), subImageOut, subImage, modKernel, casa::ImageConvolver<T>::NONE, 1.0, true);
        } else {
            brightnessUnitOut = imageIn.units().getName();
            beamOut = inputBeam;
            subImageOut.put(subImage.get());
        }

        {
            auto doMask = imageOut->isMasked() && imageOut->hasPixelMask();
            Lattice<Bool>* pMaskOut = 0;
            if (doMask) {
                pMaskOut = &imageOut->pixelMask();
                if (!pMaskOut->isWritable()) {
                    doMask = false;
                }
            }

            auto cursorShape = subImageOut.niceCursorShape();
            auto outPos = start;
            LatticeStepper stepper(subImageOut.shape(), cursorShape, casacore::LatticeStepper::RESIZE);
            RO_MaskedLatticeIterator<T> iter(subImageOut, stepper);
            for (iter.reset(); !iter.atEnd(); iter++) {
                casacore::IPosition cursorShape = iter.cursorShape();
                imageOut->putSlice(iter.cursor(), outPos);
                if (doMask) {
                    pMaskOut->putSlice(iter.getMask(), outPos);
                }
                outPos = outPos + cursorShape;
            }
        }

        if (!_targetres) {
            iiOut.setBeam(channel, polarization, beamOut);
        }
    }
}

template <class T>
Double Image2DConvolver<T>::_makeKernel(casacore::Array<Double>& kernelArray, casacore::VectorKernel::KernelTypes kernelType,
    const std::vector<casacore::Quantity>& parameters, const casacore::ImageInterface<T>& imageIn) const {
    // Check number of parameters
    _checkKernelParameters(kernelType, parameters);

    // Convert kernel widths to pixels from world.  Demands major and minor both in pixels or both in world, else exception
    casacore::Vector<casacore::Double> dParameters;
    const casacore::CoordinateSystem cSys = imageIn.coordinates();

    // Use the reference value for the shape conversion direction
    casacore::Vector<casacore::Quantity> wParameters(5);
    for (casacore::uInt i = 0; i < 3; i++) {
        wParameters(i + 2) = parameters[i];
    }

    const casacore::Vector<casacore::Double> refVal = cSys.referenceValue();
    const casacore::Vector<casacore::String> units = cSys.worldAxisUnits();
    casacore::Int wAxis = cSys.pixelAxisToWorldAxis(_axes(0));
    wParameters(0) = casacore::Quantity(refVal(wAxis), units(wAxis));
    wAxis = cSys.pixelAxisToWorldAxis(_axes(1));
    wParameters(1) = casacore::Quantity(refVal(wAxis), units(wAxis));
    casa::SkyComponentFactory::worldWidthsToPixel(dParameters, wParameters, cSys, _axes, false);

    // Create n-Dim kernel array shape
    auto kernelShape = _shapeOfKernel(kernelType, dParameters, imageIn.ndim());

    // Create kernel array. We will fill the n-Dim array (shape non-unity only for pixelAxes) through its 2D casacore::Matrix incarnation.
    // Aren't we clever.
    kernelArray = 0;
    kernelArray.resize(kernelShape);
    auto kernelArray2 = kernelArray.nonDegenerate(_axes);
    auto kernelMatrix = static_cast<casacore::Matrix<Double>>(kernelArray2);

    // Fill kernel casacore::Matrix with functional (height unity)
    return _fillKernel(kernelMatrix, kernelType, kernelShape, dParameters);
}

template <class T>
Double Image2DConvolver<T>::_dealWithRestoringBeam(casacore::String& brightnessUnitOut, casacore::GaussianBeam& beamOut,
    const casacore::Array<Double>& kernelArray, Double kernelVolume, const casacore::VectorKernel::KernelTypes,
    const casacore::Vector<casacore::Quantity>& parameters, const casacore::CoordinateSystem& cSys, const casacore::GaussianBeam& beamIn,
    const casacore::Unit& brightnessUnitIn, casacore::Bool emitMessage) const {
    // Find out if convolution axes hold the sky. Scaling from Jy/beam and Jy/pixel only really makes sense if this is true
    casacore::Bool holdsOneSkyAxis;
    auto hasSky = casacore::CoordinateUtil::holdsSky(holdsOneSkyAxis, cSys, _axes.asVector());
    if (hasSky) {
        const casacore::DirectionCoordinate dc = cSys.directionCoordinate();
        auto inc = dc.increment();
        auto unit = dc.worldAxisUnits();
        casacore::Quantity x(inc[0], unit[0]);
        casacore::Quantity y(inc[1], unit[1]);
        auto diag = sqrt(x * x + y * y);
        auto minAx = parameters[1];
        if (minAx.getUnit().startsWith("pix")) {
            minAx.setValue(minAx.getValue() * x.getValue());
            minAx.setUnit(x.getUnit());
        }
        if (minAx < diag) {
            diag.convert(minAx.getFullUnit());
            spdlog::debug(
                "Convolving kernel has minor axis {} which is less than the pixel diagonal length of {}. Thus, the kernel is "
                "poorly sampled, and so the output of this application may not be what you expect. You should consider increasing the "
                "kernel size or regridding the image to a smaller pixel size",
                FormatQuantity(minAx), FormatQuantity(diag));
        } else if (beamIn.getMinor() < diag && beamIn != casacore::GaussianBeam::NULL_BEAM) {
            diag.convert(beamIn.getMinor().getFullUnit());
            spdlog::debug(
                "Input beam has minor axis {} which is less than the pixel diagonal length of {}. Thus, the beam is poorly "
                "sampled, and so the output of this application may not be what you expect. You should consider regridding the image "
                "to a smaller pixel size.",
                FormatQuantity(beamIn.getMinor()), FormatQuantity(diag));
        }
    }

    if (emitMessage) {
        string tmp = (hasSky ? "" : "not");
        spdlog::debug("You are {} convolving the sky", tmp);
    }

    beamOut = casacore::GaussianBeam();
    auto bUnitIn = upcase(brightnessUnitIn.getName());
    const auto& refPix = cSys.referencePixel();
    Double scaleFactor = 1;
    brightnessUnitOut = brightnessUnitIn.getName();
    auto autoScale = _scale <= 0;

    if (hasSky && bUnitIn.contains("/PIXEL")) {
        // Easy case. Peak of convolution kernel must be unity and output units are Jy/beam. All other cases require numerical convolution
        // of beams
        brightnessUnitOut = "Jy/beam";

        // Exception already generated if only one of major and minor in pixel units
        auto majAx = parameters(0);
        auto minAx = parameters(1);

        if (majAx.getFullUnit().getName() == "pix") {
            casacore::Vector<casacore::Double> pixelParameters(5);
            pixelParameters(0) = refPix(_axes(0));
            pixelParameters(1) = refPix(_axes(1));
            pixelParameters(2) = parameters(0).getValue();
            pixelParameters(3) = parameters(1).getValue();
            pixelParameters(4) = parameters(2).getValue(casacore::Unit("rad"));
            casacore::GaussianBeam worldParameters;
            casa::SkyComponentFactory::pixelWidthsToWorld(worldParameters, pixelParameters, cSys, _axes, false);
            majAx = worldParameters.getMajor();
            minAx = worldParameters.getMinor();
        }

        beamOut = casacore::GaussianBeam(majAx, minAx, parameters(2));

        // casacore::Input p.a. is positive N->E
        if (!autoScale) {
            scaleFactor = _scale;
            spdlog::warn("Autoscaling is recommended for Jy/pixel convolution.");
        }
    } else {
        // Is there an input restoring beam and are we convolving the sky to which it pertains?  If not, all we can do is use user scaling
        // or normalize the convolution kernel to unit volume.  There is no point to convolving the input beam either as it pertains only to
        // the sky
        if (hasSky && !beamIn.isNull()) {
            // Convert restoring beam parameters to pixels. Output pa is pos +x -> +y in pixel frame.
            casacore::Vector<casacore::Quantity> wParameters(5);
            const auto refVal = cSys.referenceValue();
            const auto units = cSys.worldAxisUnits();
            auto wAxis = cSys.pixelAxisToWorldAxis(_axes(0));
            wParameters(0) = casacore::Quantity(refVal(wAxis), units(wAxis));
            wAxis = cSys.pixelAxisToWorldAxis(_axes(1));
            wParameters(1) = casacore::Quantity(refVal(wAxis), units(wAxis));
            wParameters(2) = beamIn.getMajor();
            wParameters(3) = beamIn.getMinor();
            wParameters(4) = beamIn.getPA(true);
            casacore::Vector<casacore::Double> dParameters;
            casa::SkyComponentFactory::worldWidthsToPixel(dParameters, wParameters, cSys, _axes, false);

            // Create 2-D beam array shape
            // casacore::IPosition dummyAxes(2, 0, 1);
            auto beamShape = _shapeOfKernel(casacore::VectorKernel::GAUSSIAN, dParameters, 2);

            // Create beam casacore::Matrix and fill with height unity
            casacore::Matrix<Double> beamMatrixIn(beamShape(0), beamShape(1));
            _fillKernel(beamMatrixIn, casacore::VectorKernel::GAUSSIAN, beamShape, dParameters);
            auto shape = beamMatrixIn.shape();

            // Get 2-D version of convolution kenrel
            auto kernelArray2 = kernelArray.nonDegenerate(_axes);
            auto kernelMatrix = static_cast<casacore::Matrix<Double>>(kernelArray2);

            // Convolve input restoring beam array by convolution kernel array
            casacore::Matrix<Double> beamMatrixOut;
            casacore::Convolver<Double> conv(beamMatrixIn, kernelMatrix.shape());
            conv.linearConv(beamMatrixOut, kernelMatrix);

            // Scale kernel
            auto maxValOut = max(beamMatrixOut);
            scaleFactor = autoScale ? 1 / maxValOut : _scale;

            Fit2D fitter(*this->_getLog());
            const casacore::uInt n = beamMatrixOut.shape()(0);
            auto bParameters = fitter.estimate(casacore::Fit2D::GAUSSIAN, beamMatrixOut);
            casacore::Vector<casacore::Bool> bParameterMask(bParameters.nelements(), true);
            bParameters(1) = (n - 1) / 2;    // x centre
            bParameters(2) = bParameters(1); // y centre

            // Set range so we don't include too many pixels in fit which will make it very slow
            fitter.addModel(casacore::Fit2D::GAUSSIAN, bParameters, bParameterMask);
            casacore::Array<Double> sigma;
            fitter.setIncludeRange(maxValOut / 10.0, maxValOut + 0.1);
            auto error = fitter.fit(beamMatrixOut, sigma);
            ThrowIf(error == casacore::Fit2D::NOCONVERGE || error == casacore::Fit2D::FAILED || error == casacore::Fit2D::NOGOOD,
                "Failed to fit the output beam");

            auto bSolution = fitter.availableSolution();
            // Convert to world units.
            casacore::Vector<casacore::Double> pixelParameters(5);
            pixelParameters(0) = refPix(_axes(0));
            pixelParameters(1) = refPix(_axes(1));
            pixelParameters(2) = bSolution(3);
            pixelParameters(3) = bSolution(4);
            pixelParameters(4) = bSolution(5);
            casa::SkyComponentFactory::pixelWidthsToWorld(beamOut, pixelParameters, cSys, _axes, false);

            if (!brightnessUnitIn.getName().contains(casacore::Regex(Regex::makeCaseInsensitive("beam")))) {
                scaleFactor *= beamIn.getArea("arcsec2") / beamOut.getArea("arcsec2");
            }
        } else {
            if (autoScale) {
                // Conserve flux is the best we can do
                scaleFactor = 1 / kernelVolume;
            } else {
                scaleFactor = _scale;
            }
        }
    }

    // Put beam position angle into range +/- 180 in case it has eluded us so far
    if (!beamOut.isNull()) {
        casacore::MVAngle pa(beamOut.getPA(true).getValue(casacore::Unit("rad")));
        pa();
        beamOut = casacore::GaussianBeam(beamOut.getMajor(), beamOut.getMinor(), casacore::Quantity(pa.degree(), casacore::Unit("deg")));
    }

    return scaleFactor;
}

template <class T>
void Image2DConvolver<T>::_checkKernelParameters(
    casacore::VectorKernel::KernelTypes kernelType, const casacore::Vector<casacore::Quantity>& parameters) const {
    if (kernelType == casacore::VectorKernel::BOXCAR) {
        ThrowCc("Boxcar kernel not yet implemented");
        ThrowIf(parameters.nelements() != 3, "Boxcar kernels require 3 parameters");
    } else if (kernelType == casacore::VectorKernel::GAUSSIAN) {
        ThrowIf(parameters.nelements() != 3, "Gaussian kernels require exactly 3 parameters");
    } else {
        ThrowCc("The kernel type " + casacore::VectorKernel::fromKernelType(kernelType) + " is not supported");
    }
}

template <class T>
casacore::IPosition Image2DConvolver<T>::_shapeOfKernel(const casacore::VectorKernel::KernelTypes kernelType,
    const casacore::Vector<casacore::Double>& parameters, const casacore::uInt ndim) const {
    // Work out how big the array holding the kernel should be simplest algorithm possible. Shape is presently square.

    // Find 2D shape
    casacore::uInt n;
    if (kernelType == casacore::VectorKernel::GAUSSIAN) {
        casacore::uInt n1 = _sizeOfGaussian(parameters(0), 5.0);
        casacore::uInt n2 = _sizeOfGaussian(parameters(1), 5.0);
        n = max(n1, n2);
        if (n % 2 == 0)
            n++; // Make shape odd so centres well
    } else if (kernelType == casacore::VectorKernel::BOXCAR) {
        n = 2 * casacore::Int(max(parameters(0), parameters(1)) + 0.5);
        if (n % 2 == 0)
            n++; // Make shape odd so centres well
    } else {
        throw(casacore::AipsError("Unrecognized kernel type")); // Earlier checking should prevent this
    }

    // Now find the shape for the image and slot the 2D shape in in the correct axis locations
    casacore::IPosition shape(ndim, 1);
    shape(_axes(0)) = n;
    shape(_axes(1)) = n;
    return shape;
}

template <class T>
uInt Image2DConvolver<T>::_sizeOfGaussian(const casacore::Double width, const casacore::Double nSigma) const {
    // +/- 5 sigma is a volume error of less than 6e-5%
    casacore::Double sigma = width / sqrt(casacore::Double(8.0) * C::ln2);
    return (casacore::Int(nSigma * sigma + 0.5) + 1) * 2;
}

template <class T>
Double Image2DConvolver<T>::_fillKernel(casacore::Matrix<Double>& kernelMatrix, casacore::VectorKernel::KernelTypes kernelType,
    const casacore::IPosition& kernelShape, const casacore::Vector<casacore::Double>& parameters) const {
    // Centre functional in array (shape is odd)
    auto xCentre = Double((kernelShape[_axes[0]] - 1) / 2.0);
    auto yCentre = Double((kernelShape[_axes[1]] - 1) / 2.0);
    Double height = 1;

    // Create functional. We only have gaussian2d functions at this point.  Later the filling code can be moved out of the if statement
    Double maxValKernel;
    Double volumeKernel = 0;
    auto pa = parameters[2];
    auto ratio = parameters[1] / parameters[0];
    auto major = parameters[0];

    if (kernelType == casacore::VectorKernel::GAUSSIAN) {
        _fillGaussian(maxValKernel, volumeKernel, kernelMatrix, height, xCentre, yCentre, major, ratio, pa);
    } else if (kernelType == casacore::VectorKernel::BOXCAR) {
        ThrowCc("Boxcar convolution not supported");
    } else {
        // Earlier checking should prevent this
        ThrowCc("Unrecognized kernel type");
    }
    return volumeKernel;
}

template <class T>
void Image2DConvolver<T>::_fillGaussian(Double& maxVal, Double& volume, casacore::Matrix<Double>& pixels, Double height, Double xCentre,
    Double yCentre, Double majorAxis, Double ratio, Double positionAngle) const {
    //
    // pa positive in +x ->+y pixel coordinate frame
    //
    casacore::uInt n1 = pixels.shape()(0);
    casacore::uInt n2 = pixels.shape()(1);
    AlwaysAssert(n1 == n2, casacore::AipsError);
    positionAngle += C::pi_2; // +y -> -x
    casacore::Gaussian2D<Double> g2d(height, xCentre, yCentre, majorAxis, ratio, positionAngle);
    maxVal = -1.0e30;
    volume = 0.0;
    casacore::Vector<Double> pos(2);
    for (casacore::uInt j = 0; j < n1; ++j) {
        pos[1] = j;
        for (casacore::uInt i = 0; i < n1; ++i) {
            pos[0] = i;
            Double val = g2d(pos);
            pixels(i, j) = val;
            maxVal = max(val, maxVal);
            volume += val;
        }
    }
}

template <class T>
void Image2DConvolver<T>::StopCalculation() {
    _stop = true;
}

#endif // CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_
