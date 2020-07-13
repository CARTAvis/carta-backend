//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/ImageMoments.tcc"
//
#ifndef CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_TCC_
#define CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_TCC_

using namespace carta;

template <class T>
ImageMoments<T>::ImageMoments(
    const casacore::ImageInterface<T>& image, casacore::LogIO& os, casacore::Bool overWriteOutput, casacore::Bool showProgressU)
    : MomentsBase<T>(os, overWriteOutput, showProgressU), _stop(false) {
    setNewImage(image);
}

template <class T>
ImageMoments<T>::~ImageMoments() {}

template <class T>
casacore::Bool ImageMoments<T>::setNewImage(const casacore::ImageInterface<T>& image) {
    T* dummy = nullptr;
    casacore::DataType imageType = whatType(dummy);

    ThrowIf(imageType != casacore::TpFloat && imageType != casacore::TpDouble,
        "Moments can only be evaluated for Float or Double valued images");

    // Make a clone of the image
    _image.reset(image.cloneII());
    return true;
}

template <class T>
casacore::Bool ImageMoments<T>::setMomentAxis(const casacore::Int momentAxisU) {
    if (!goodParameterStatus_p) {
        throw casacore::AipsError("Internal class status is bad");
    }

    momentAxis_p = momentAxisU;

    if (momentAxis_p == momentAxisDefault_p) {
        momentAxis_p = _image->coordinates().spectralAxisNumber();
        if (momentAxis_p == -1) {
            goodParameterStatus_p = false;
            throw casacore::AipsError("There is no spectral axis in this image -- specify the axis");
        }

    } else {
        if (momentAxis_p < 0 || momentAxis_p > casacore::Int(_image->ndim() - 1)) {
            goodParameterStatus_p = false;
            throw casacore::AipsError("Illegal moment axis; out of range");
        }
        if (_image->shape()(momentAxis_p) <= 0) {
            goodParameterStatus_p = false;
            throw casacore::AipsError("Illegal moment axis; it has no pixels");
        }
    }

    if (momentAxis_p == _image->coordinates().spectralAxisNumber() && _image->imageInfo().hasMultipleBeams()) {
        casacore::GaussianBeam maxBeam = casa::CasaImageBeamSet(_image->imageInfo().getBeamSet()).getCommonBeam();
        os_p << casacore::LogIO::NORMAL << "The input image has multiple beams so each "
             << "plane will be convolved to the largest beam size " << maxBeam << " prior to calculating moments" << casacore::LogIO::POST;

        casa::Image2DConvolver<casacore::Float> convolver(_image, nullptr, "", "", false);
        auto dirAxes = _image->coordinates().directionAxesNumbers();
        convolver.setAxes(std::make_pair(dirAxes[0], dirAxes[1]));
        convolver.setKernel("gaussian", maxBeam.getMajor(), maxBeam.getMinor(), maxBeam.getPA(true));
        convolver.setScale(-1);
        convolver.setTargetRes(true);
        auto imageCopy = convolver.convolve();

        // replace the input image pointer with the convolved image pointer
        // and proceed using the convolved image as if it were the input image
        _image = imageCopy;
    }

    worldMomentAxis_p = _image->coordinates().pixelAxisToWorldAxis(momentAxis_p);

    return true;
}

template <class T>
casacore::Bool ImageMoments<T>::setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxesU,
    const casacore::Vector<casacore::Int>& kernelTypesU, const casacore::Vector<casacore::Quantum<casacore::Double>>& kernelWidthsU) {
    if (!goodParameterStatus_p) {
        error_p = "Internal class status is bad";
        return false;
    }

    // First check the smoothing axes
    casacore::Int i;
    if (smoothAxesU.nelements() > 0) {
        smoothAxes_p = smoothAxesU;
        for (i = 0; i < casacore::Int(smoothAxes_p.nelements()); i++) {
            if (smoothAxes_p(i) < 0 || smoothAxes_p(i) > casacore::Int(_image->ndim() - 1)) {
                error_p = "Illegal smoothing axis given";
                goodParameterStatus_p = false;
                return false;
            }
        }
        doSmooth_p = true;
    } else {
        doSmooth_p = false;
        return true;
    }

    // Now check the smoothing types
    if (kernelTypesU.nelements() > 0) {
        kernelTypes_p = kernelTypesU;
        for (i = 0; i < casacore::Int(kernelTypes_p.nelements()); i++) {
            if (kernelTypes_p(i) < 0 || kernelTypes_p(i) > casacore::VectorKernel::NKERNELS - 1) {
                error_p = "Illegal smoothing kernel types given";
                goodParameterStatus_p = false;
                return false;
            }
        }
    } else {
        error_p = "Smoothing kernel types were not given";
        goodParameterStatus_p = false;
        return false;
    }

    // Check user gave us enough smoothing types
    if (smoothAxesU.nelements() != kernelTypes_p.nelements()) {
        error_p = "Different number of smoothing axes to kernel types";
        goodParameterStatus_p = false;
        return false;
    }

    // Now the desired smoothing kernels widths.
    // Allow for Hanning to not be given as it is always 1/4, 1/2, 1/4
    kernelWidths_p.resize(smoothAxes_p.nelements());
    casacore::Int nK = kernelWidthsU.size();
    for (i = 0; i < casacore::Int(smoothAxes_p.nelements()); i++) {
        if (kernelTypes_p(i) == casacore::VectorKernel::HANNING) {
            // For Hanning, width is always 3pix
            casacore::Quantity tmp(3.0, casacore::String("pix"));
            kernelWidths_p(i) = tmp;

        } else if (kernelTypes_p(i) == casacore::VectorKernel::BOXCAR) {
            // For box must be odd number greater than 1
            if (i > nK - 1) {
                error_p = "Not enough smoothing widths given";
                goodParameterStatus_p = false;
                return false;
            } else {
                kernelWidths_p(i) = kernelWidthsU(i);
            }

        } else if (kernelTypes_p(i) == casacore::VectorKernel::GAUSSIAN) {
            if (i > nK - 1) {
                error_p = "Not enough smoothing widths given";
                goodParameterStatus_p = false;
                return false;
            } else {
                kernelWidths_p(i) = kernelWidthsU(i);
            }

        } else {
            error_p = "Internal logic error";
            goodParameterStatus_p = false;
            return false;
        }
    }

    return true;
}

template <class T>
casacore::Bool ImageMoments<T>::setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxesU,
    const casacore::Vector<casacore::Int>& kernelTypesU, const casacore::Vector<casacore::Double>& kernelWidthsPix) {
    return MomentsBase<T>::setSmoothMethod(smoothAxesU, kernelTypesU, kernelWidthsPix);
}

template <class T>
std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> ImageMoments<T>::createMoments(
    casacore::Bool doTemp, const casacore::String& outName, casacore::Bool removeAxis) {
    casacore::LogOrigin myOrigin("ImageMoments", __func__);
    os_p << myOrigin;

    if (!goodParameterStatus_p) {
        // FIXME goodness, why are we waiting so long to throw an exception if this is the case?
        throw casacore::AipsError("Internal status of class is bad.  You have ignored errors");
    }

    // Find spectral axis use a copy of the coordinate system here since, if the image has multiple beams, _image
    // will change and hence a reference to its casacore::CoordinateSystem will disappear causing a seg fault.
    casacore::CoordinateSystem cSys = _image->coordinates();
    casacore::Int spectralAxis = cSys.spectralAxisNumber(false);
    if (momentAxis_p == momentAxisDefault_p) {
        this->setMomentAxis(spectralAxis);
        if (_image->shape()(momentAxis_p) <= 1) {
            goodParameterStatus_p = false;
            throw casacore::AipsError("Illegal moment axis; it has only 1 pixel");
        }
        worldMomentAxis_p = cSys.pixelAxisToWorldAxis(momentAxis_p);
    }

    convertToVelocity_p = (momentAxis_p == spectralAxis) && (cSys.spectralCoordinate().restFrequency() > 0);

    os_p << myOrigin;

    casacore::String momentAxisUnits = cSys.worldAxisUnits()(worldMomentAxis_p);

    os_p << casacore::LogIO::NORMAL << endl << "Moment axis type is " << cSys.worldAxisNames()(worldMomentAxis_p) << casacore::LogIO::POST;

    // If the moment axis is a spectral axis, indicate we want to convert to velocity. Check the user's requests are allowed
    _checkMethod();

    // Check that input and output image names aren't the same, if there is only one output image
    if (moments_p.nelements() == 1 && !doTemp) {
        if (!outName.empty() && (outName == _image->name())) {
            throw casacore::AipsError("Input image and output image have same name");
        }
    }

    auto smoothClipMethod = false;
    auto windowMethod = false;
    auto fitMethod = false;
    auto clipMethod = false;

    if (doSmooth_p && !doWindow_p) {
        smoothClipMethod = true;
    } else if (doWindow_p) {
        windowMethod = true;
    } else if (doFit_p) {
        fitMethod = true;
    } else {
        clipMethod = true;
    }

    // We only smooth the image if we are doing the smooth/clip method or possibly the interactive window method.
    // Note that the convolution routines can only handle convolution when the image fits fully in core at present.
    SPIIT smoothedImage;
    if (doSmooth_p) {
        smoothedImage = _smoothImage();
    }

    // Set output images shape and coordinates.
    casacore::IPosition outImageShape;
    const auto cSysOut = this->_makeOutputCoordinates(outImageShape, cSys, _image->shape(), momentAxis_p, removeAxis);
    auto nMoments = moments_p.nelements();

    // Resize the vector of pointers for output images
    std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> outPt(nMoments);

    // Loop over desired output moments
    casacore::String suffix;
    casacore::Bool goodUnits;
    casacore::Bool giveMessage = true;
    const auto imageUnits = _image->units();

    for (casacore::uInt i = 0; i < nMoments; ++i) {
        // Set moment image units and assign pointer to output moments array Value of goodUnits is the same for each output moment image
        casacore::Unit momentUnits;
        goodUnits = this->_setOutThings(suffix, momentUnits, imageUnits, momentAxisUnits, moments_p(i), convertToVelocity_p);

        // Create output image(s). Either casacore::PagedImage or TempImage
        SPIIT imgp;

        if (!doTemp) {
            const casacore::String in = _image->name(false);
            casacore::String outFileName;

            if (moments_p.size() == 1) {
                if (outName.empty()) {
                    outFileName = in + suffix;
                } else {
                    outFileName = outName;
                }
            } else {
                if (outName.empty()) {
                    outFileName = in + suffix;
                } else {
                    outFileName = outName + suffix;
                }
            }

            if (!overWriteOutput_p) {
                casacore::NewFile x;
                casacore::String error;
                if (!x.valueOK(outFileName, error)) {
                    throw casacore::AipsError(error);
                }
            }

            imgp.reset(new casacore::PagedImage<T>(outImageShape, cSysOut, outFileName));
            os_p << casacore::LogIO::NORMAL << "Created " << outFileName << casacore::LogIO::POST;

        } else {
            imgp.reset(new casacore::TempImage<T>(casacore::TiledShape(outImageShape), cSysOut));
            os_p << casacore::LogIO::NORMAL << "Created TempImage" << casacore::LogIO::POST;
        }

        ThrowIf(!imgp, "Failed to create output file");
        imgp->setMiscInfo(_image->miscInfo());
        imgp->setImageInfo(_image->imageInfo());
        imgp->appendLog(_image->logger());
        imgp->makeMask("mask0", true, true);

        // Set output image units if possible
        if (goodUnits) {
            imgp->setUnits(momentUnits);
        } else {
            if (giveMessage) {
                os_p << casacore::LogIO::NORMAL << "Could not determine the units of the moment image(s) so the units" << endl;
                os_p << "will be the same as those of the input image. This may not be very useful." << casacore::LogIO::POST;
                giveMessage = false;
            }
        }

        outPt[i] = imgp;
    }

    // If the user is using the automatic, non-fitting window method, they need a good assessment of the noise.
    // The user can input that value, but if they don't, we work it out here.
    T noise;
    if (stdDeviation_p <= T(0) && (doWindow_p || (doFit_p && !doWindow_p))) {
        if (smoothedImage) {
            os_p << casacore::LogIO::NORMAL << "Evaluating noise level from smoothed image" << casacore::LogIO::POST;
            _whatIsTheNoise(noise, *smoothedImage);

        } else {
            os_p << casacore::LogIO::NORMAL << "Evaluating noise level from input image" << casacore::LogIO::POST;
            _whatIsTheNoise(noise, *_image);
        }
        stdDeviation_p = noise;
    }

    // Create appropriate MomentCalculator object
    os_p << casacore::LogIO::NORMAL << "Begin computation of moments" << casacore::LogIO::POST;

    shared_ptr<MomentCalcBase<T>> momentCalculator;
    if (clipMethod || smoothClipMethod) {
        momentCalculator.reset(new MomentClip<T>(smoothedImage, *this, os_p, outPt.size()));

    } else if (windowMethod) {
        momentCalculator.reset(new MomentWindow<T>(smoothedImage, *this, os_p, outPt.size()));

    } else if (fitMethod) {
        momentCalculator.reset(new MomentFit<T>(*this, os_p, outPt.size()));
    }

    // Iterate optimally through the image, compute the moments, fill the output lattices
    unique_ptr<casa::ImageMomentsProgress> pProgressMeter;
    if (showProgress_p) {
        pProgressMeter.reset(new casa::ImageMomentsProgress());
        if (_progressMonitor) {
            pProgressMeter->setProgressMonitor(_progressMonitor);
        }
    }

    casacore::uInt n = outPt.size();
    casacore::PtrBlock<casacore::MaskedLattice<T>*> ptrBlock(n);
    for (casacore::uInt i = 0; i < n; ++i) {
        ptrBlock[i] = outPt[i].get();
    }

    // Do expensive calculation
    lineMultiApply(ptrBlock, *_image, *momentCalculator, momentAxis_p, pProgressMeter.get());

    if (windowMethod || fitMethod) {
        if (momentCalculator->nFailedFits() != 0) {
            os_p << casacore::LogIO::NORMAL << "There were " << momentCalculator->nFailedFits() << " failed fits" << casacore::LogIO::POST;
        }
    }

    if (_stop) {
        // Clear the output image ptr vector if calculation is cancelled
        outPt.clear();
    } else {
        for (auto& p : outPt) {
            p->flush();
        }
    }

    return outPt;
}

// casacore::Smooth image. casacore::Input masked pixels are zeros before smoothing.
// The output smoothed image is masked as well to reflect the input mask.
template <class T>
SPIIT ImageMoments<T>::_smoothImage() {
    auto axMax = max(smoothAxes_p) + 1;
    ThrowIf(axMax > casacore::Int(_image->ndim()), "You have specified an illegal smoothing axis");

    SPIIT smoothedImage;
    if (smoothOut_p.empty()) {
        smoothedImage.reset(new casacore::TempImage<T>(_image->shape(), _image->coordinates()));
    } else {
        // This image has already been checked in setSmoothOutName
        // to not exist
        smoothedImage.reset(new casacore::PagedImage<T>(_image->shape(), _image->coordinates(), smoothOut_p));
    }

    smoothedImage->setMiscInfo(_image->miscInfo());

    // Do the convolution. Conserve flux.
    casa::SepImageConvolver<T> sic(*_image, os_p, true);
    auto n = smoothAxes_p.size();
    for (casacore::uInt i = 0; i < n; ++i) {
        casacore::VectorKernel::KernelTypes type = casacore::VectorKernel::KernelTypes(kernelTypes_p[i]);
        sic.setKernel(casacore::uInt(smoothAxes_p[i]), type, kernelWidths_p[i], true, false, 1.0);
    }
    sic.convolve(*smoothedImage);

    return smoothedImage;
}

// Determine the noise level in the image by first making a histogram of
// the image, then fitting a Gaussian between the 25% levels to give sigma
// Find a histogram of the image
template <class T>
void ImageMoments<T>::_whatIsTheNoise(T& sigma, const casacore::ImageInterface<T>& image) {
    casa::ImageHistograms<T> histo(image, false);
    const casacore::uInt nBins = 100;
    histo.setNBins(nBins);

    // It is safe to use casacore::Vector rather than casacore::Array because
    // we are binning the whole image and ImageHistograms will only resize
    // these Vectors to a 1-D shape
    casacore::Vector<T> values, counts;
    ThrowIf(!histo.getHistograms(values, counts), "Unable to make histogram of image");

    // Enter into a plot/fit loop
    auto binWidth = values(1) - values(0);
    T xMin, xMax, yMin, yMax;
    xMin = values(0) - binWidth;
    xMax = values(nBins - 1) + binWidth;
    casacore::Float xMinF = casacore::Float(real(xMin));
    casacore::Float xMaxF = casacore::Float(real(xMax));
    casacore::LatticeStatsBase::stretchMinMax(xMinF, xMaxF);
    casacore::IPosition yMinPos(1), yMaxPos(1);
    minMax(yMin, yMax, yMinPos, yMaxPos, counts);
    casacore::Float yMaxF = casacore::Float(real(yMax));
    yMaxF += yMaxF / 20;
    auto first = true;
    auto more = true;

    while (more) {
        casacore::Int iMin = 0;
        casacore::Int iMax = 0;

        if (first) {
            first = false;

            iMax = yMaxPos(0);
            casacore::uInt i;
            for (i = yMaxPos(0); i < nBins; i++) {
                if (counts(i) < yMax / 4) {
                    iMax = i;
                    break;
                }
            }

            iMin = yMinPos(0);
            for (i = yMaxPos(0); i > 0; i--) {
                if (counts(i) < yMax / 4) {
                    iMin = i;
                    break;
                }
            }

            // Check range is sensible
            if (iMax <= iMin || abs(iMax - iMin) < 3) {
                os_p << casacore::LogIO::NORMAL << "The image histogram is strangely shaped, fitting to all bins" << casacore::LogIO::POST;
                iMin = 0;
                iMax = nBins - 1;
            }
        }

        // Now generate the distribution we want to fit.  Normalize to
        // peak 1 to help fitter.
        const casacore::uInt nPts2 = iMax - iMin + 1;
        casacore::Vector<T> xx(nPts2);
        casacore::Vector<T> yy(nPts2);
        casacore::Int i;

        for (i = iMin; i <= iMax; i++) {
            xx(i - iMin) = values(i);
            yy(i - iMin) = counts(i) / yMax;
        }

        // Create fitter
        casacore::NonLinearFitLM<T> fitter;
        casacore::Gaussian1D<casacore::AutoDiff<T>> gauss;
        fitter.setFunction(gauss);

        // Initial guess
        casacore::Vector<T> v(3);
        v(0) = 1.0;                  // height
        v(1) = values(yMaxPos(0));   // position
        v(2) = nPts2 * binWidth / 2; // width

        // Fit
        fitter.setParameterValues(v);
        fitter.setMaxIter(50);
        T tol = 0.001;
        fitter.setCriteria(tol);
        casacore::Vector<T> resultSigma(nPts2);
        resultSigma = 1;
        casacore::Vector<T> solution;
        casacore::Bool fail = false;

        try {
            solution = fitter.fit(xx, yy, resultSigma);
        } catch (const casacore::AipsError& x) {
            fail = true;
        }

        // Return values of fit
        if (!fail && fitter.converged()) {
            sigma = T(abs(solution(2)) / C::sqrt2);
            os_p << casacore::LogIO::NORMAL << "*** The fitted standard deviation of the noise is " << sigma << endl
                 << casacore::LogIO::POST;
        } else {
            os_p << casacore::LogIO::WARN << "The fit to determine the noise level failed." << endl;
            os_p << "Try inputting it directly" << endl;
        }

        // Another go
        more = false;
    }
}

template <class T>
void ImageMoments<T>::setProgressMonitor(casa::ImageMomentsProgressMonitor* monitor) {
    _progressMonitor = monitor;
}

template <class T>
void ImageMoments<T>::lineMultiApply(PtrBlock<MaskedLattice<T>*>& latticeOut, const MaskedLattice<T>& latticeIn,
    LineCollapser<T, T>& collapser, uInt collapseAxis, LatticeProgress* tellProgress) {
    // First verify that all the output lattices have the same shape and tile shape
    const uInt nOut = latticeOut.nelements();
    AlwaysAssert(nOut > 0, AipsError);

    const IPosition shape(latticeOut[0]->shape());
    const uInt outDim = shape.nelements();
    for (uInt i = 1; i < nOut; ++i) {
        AlwaysAssert(latticeOut[i]->shape() == shape, AipsError);
    }

    const IPosition& inShape = latticeIn.shape();
    IPosition outPos(outDim, 0);
    IPosition outShape(outDim, 1);

    // Does the input has a mask?
    // If not, can the collapser handle a null mask.
    Bool useMask = latticeIn.isMasked() ? True : (!collapser.canHandleNullMask());
    const uInt inNDim = inShape.size();
    const IPosition displayAxes = IPosition::makeAxisPath(inNDim).otherAxes(inNDim, IPosition(1, collapseAxis));
    const uInt nDisplayAxes = displayAxes.size();
    Vector<T> result(nOut);
    Vector<Bool> resultMask(nOut);

    // read in larger chunks than before, because that was very
    // Inefficient and brought NRAO cluster to a snail's pace,
    // and then do the accounting for the input lines in memory
    IPosition chunkSliceStart(inNDim, 0);
    IPosition chunkSliceEnd = chunkSliceStart;
    chunkSliceEnd[collapseAxis] = inShape[collapseAxis] - 1;
    const IPosition chunkSliceEndAtChunkIterBegin = chunkSliceEnd;
    IPosition chunkShapeInit = _chunkShape(collapseAxis, latticeIn);
    LatticeStepper myStepper(inShape, chunkShapeInit, LatticeStepper::RESIZE);
    RO_MaskedLatticeIterator<T> latIter(latticeIn, myStepper);

    IPosition curPos;
    static const Vector<Bool> noMask;

    if (tellProgress) {
        uInt nExpectedIters = inShape.product() / chunkShapeInit.product();
        tellProgress->init(nExpectedIters);
    }

    uInt nDone = 0;
    for (latIter.reset(); !latIter.atEnd(); ++latIter) {
        if (_stop) {
            break;
        }

        const IPosition cp = latIter.position();
        const Array<T>& chunk = latIter.cursor();
        IPosition chunkShape = chunk.shape();
        const Array<Bool> maskChunk = useMask ? latIter.getMask() : Array<Bool>();

        chunkSliceStart = 0;
        chunkSliceEnd = chunkSliceEndAtChunkIterBegin;
        IPosition resultArrayShape = chunkShape;
        resultArrayShape[collapseAxis] = 1;
        std::vector<Array<T>> resultArray(nOut);
        std::vector<Array<Bool>> resultArrayMask(nOut);

        // need to initialize this way rather than doing it in the constructor,
        // because using a single Array in the constructor means that all Arrays
        // in the vector reference the same Array.
        for (uInt k = 0; k < nOut; k++) {
            resultArray[k] = Array<T>(resultArrayShape);
            resultArrayMask[k] = Array<Bool>(resultArrayShape);
        }

        Bool done = False;
        while (!done) {
            if (_stop) {
                break;
            }

            Vector<T> data(chunk(chunkSliceStart, chunkSliceEnd));
            Vector<Bool> mask = useMask ? Vector<Bool>(maskChunk(chunkSliceStart, chunkSliceEnd)) : noMask;
            curPos = cp + chunkSliceStart;

            collapser.multiProcess(result, resultMask, data, mask, curPos);

            for (uInt k = 0; k < nOut; ++k) {
                resultArray[k](chunkSliceStart) = result[k];
                resultArrayMask[k](chunkSliceStart) = resultMask[k];
            }

            done = True;

            for (uInt k = 0; k < nDisplayAxes; ++k) {
                uInt dax = displayAxes[k];
                if (chunkSliceStart[dax] < chunkShape[dax] - 1) {
                    ++chunkSliceStart[dax];
                    ++chunkSliceEnd[dax];
                    done = False;
                    break;
                } else {
                    chunkSliceStart[dax] = 0;
                    chunkSliceEnd[dax] = 0;
                }
            }
        }

        // put the result arrays in the output lattices
        for (uInt k = 0; k < nOut; ++k) {
            IPosition outpos = inNDim == outDim ? cp : cp.removeAxes(IPosition(1, collapseAxis));
            Bool keepAxis = resultArray[k].ndim() == latticeOut[k]->ndim();
            if (!keepAxis) {
                resultArray[k].removeDegenerate(displayAxes);
            }

            latticeOut[k]->putSlice(resultArray[k], outpos);

            if (latticeOut[k]->hasPixelMask()) {
                Lattice<Bool>& maskOut = latticeOut[k]->pixelMask();
                if (maskOut.isWritable()) {
                    if (!keepAxis) {
                        resultArrayMask[k].removeDegenerate(displayAxes);
                    }
                    maskOut.putSlice(resultArrayMask[k], outpos);
                }
            }
        }

        if (tellProgress != 0) {
            ++nDone;
            tellProgress->nstepsDone(nDone);
        }
    }

    if (tellProgress != 0) {
        tellProgress->done();
    }
}

template <class T>
IPosition ImageMoments<T>::_chunkShape(uInt axis, const MaskedLattice<T>& latticeIn) {
    uInt ndim = latticeIn.ndim();
    IPosition chunkShape(ndim, 1);
    IPosition latShape = latticeIn.shape();
    uInt nPixColAxis = latShape[axis];
    chunkShape[axis] = nPixColAxis;

    // arbitrary, but reasonable, max memory limit in bytes for storing arrays in bytes
    static const uInt limit = 2e7; // Set memory limit (Bytes)
    static const uInt sizeT = sizeof(T);
    static const uInt sizeBool = sizeof(Bool);
    uInt chunkMult = latticeIn.isMasked() ? sizeT + sizeBool : sizeT;
    uInt subChunkSize = chunkMult * nPixColAxis; // (Bytes)

    // integer division
    const uInt maxChunkSize = limit / subChunkSize;
    if (maxChunkSize <= 1) {
        // can only go row by row
        return chunkShape;
    }

    ssize_t x = maxChunkSize;
    for (uInt i = 0; i < ndim; ++i) {
        if (i != axis) {
            chunkShape[i] = std::min(x, latShape[i]);
            // integer division
            x /= chunkShape[i];
            if (x == 0) {
                break;
            }
        }
    }

    return chunkShape;
}

template <class T>
void ImageMoments<T>::StopCalculation() {
    _stop = true;
}

#endif // CARTA_BACKEND_ANALYSIS_IMAGEMOMENTS_TCC_