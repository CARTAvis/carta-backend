/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/ImageMoments.tcc"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGEMOMENTS_TCC_
#define CARTA_BACKEND__MOMENT_IMAGEMOMENTS_TCC_

#include "../Logger/Logger.h"
#include "Util/Casacore.h"

using namespace carta;

template <class T>
ImageMoments<T>::ImageMoments(const casacore::ImageInterface<T>& image, casacore::LogIO& os,
    casa::ImageMomentsProgressMonitor* progress_monitor, casacore::Bool over_write_output)
    : casa::MomentsBase<T>(os, over_write_output, true), _stop(false), _image_2d_convolver(nullptr), _progress_monitor(nullptr) {
    SetNewImage(image);
    if (progress_monitor) { // set the progress meter
        _progress_monitor = std::make_unique<casa::ImageMomentsProgress>();
        _progress_monitor->setProgressMonitor(progress_monitor);
    }
}

template <class T>
casacore::Bool ImageMoments<T>::SetNewImage(const casacore::ImageInterface<T>& image) {
    T* dummy = nullptr;
    casacore::DataType imageType = casacore::whatType(dummy);
    ThrowIf(imageType != casacore::TpFloat && imageType != casacore::TpDouble,
        "Moments can only be evaluated for Float or Double valued images");

    // Make a clone of the image
    _image.reset(image.cloneII());
    return true;
}

template <class T>
casacore::Bool ImageMoments<T>::setMomentAxis(const casacore::Int moment_axis) {
    if (!goodParameterStatus_p) {
        throw casacore::AipsError("Internal class status is bad");
    }

    // reset the number of steps have done for the beam convolution
    _steps_for_beam_convolution = 0;

    momentAxis_p = moment_axis;
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
        casacore::GaussianBeam max_beam = casa::CasaImageBeamSet(_image->imageInfo().getBeamSet()).getCommonBeam();
        spdlog::info(
            "The input image has multiple beams so each plane will be convolved to the largest beam size {} prior to calculating moments.",
            FormatBeam(max_beam));

        // reset the image 2D convolver
        _image_2d_convolver.reset(new Image2DConvolver<casacore::Float>(_image, nullptr, "", "", false, _progress_monitor.get()));

        // set parameters for the image 2D convolver
        auto dir_axes = _image->coordinates().directionAxesNumbers();
        _image_2d_convolver->setAxes(std::make_pair(dir_axes[0], dir_axes[1]));
        _image_2d_convolver->setKernel("gaussian", max_beam.getMajor(), max_beam.getMinor(), max_beam.getPA(true));
        _image_2d_convolver->setScale(-1);
        _image_2d_convolver->setTargetRes(true);
        auto image_copy = _image_2d_convolver->convolve();                  // do long calculation
        _steps_for_beam_convolution = _image_2d_convolver->GetTotalSteps(); // set number of steps have done for the beam convolution

        // Replace the input image pointer with the convolved image pointer and proceed using the convolved image as if it were the input
        // image
        if (!_stop) { // check cancellation
            _image = image_copy;
        }
    }

    worldMomentAxis_p = _image->coordinates().pixelAxisToWorldAxis(momentAxis_p);
    return true;
}

template <class T>
casacore::Bool ImageMoments<T>::setSmoothMethod(const casacore::Vector<casacore::Int>& smooth_axes,
    const casacore::Vector<casacore::Int>& kernel_types, const casacore::Vector<casacore::Quantum<casacore::Double>>& kernel_widths) {
    if (!goodParameterStatus_p) {
        error_p = "Internal class status is bad";
        return false;
    }

    // First check the smoothing axes
    casacore::Int i;
    if (smooth_axes.nelements() > 0) {
        smoothAxes_p = smooth_axes;
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
    if (kernel_types.nelements() > 0) {
        kernelTypes_p = kernel_types;
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
    if (smooth_axes.nelements() != kernelTypes_p.nelements()) {
        error_p = "Different number of smoothing axes to kernel types";
        goodParameterStatus_p = false;
        return false;
    }

    // Now the desired smoothing kernels widths. Allow for Hanning to not be given as it is always 1/4, 1/2, 1/4
    kernelWidths_p.resize(smoothAxes_p.nelements());
    casacore::Int kernel_widths_size = kernel_widths.size();
    for (i = 0; i < casacore::Int(smoothAxes_p.nelements()); i++) {
        if (kernelTypes_p(i) == casacore::VectorKernel::HANNING) {
            // For Hanning, width is always 3 pix
            casacore::Quantity tmp(3.0, casacore::String("pix"));
            kernelWidths_p(i) = tmp;

        } else if (kernelTypes_p(i) == casacore::VectorKernel::BOXCAR) {
            // For box must be odd number greater than 1
            if (i > kernel_widths_size - 1) {
                error_p = "Not enough smoothing widths given";
                goodParameterStatus_p = false;
                return false;
            } else {
                kernelWidths_p(i) = kernel_widths(i);
            }

        } else if (kernelTypes_p(i) == casacore::VectorKernel::GAUSSIAN) {
            if (i > kernel_widths_size - 1) {
                error_p = "Not enough smoothing widths given";
                goodParameterStatus_p = false;
                return false;
            } else {
                kernelWidths_p(i) = kernel_widths(i);
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
casacore::Bool ImageMoments<T>::setSmoothMethod(const casacore::Vector<casacore::Int>& smooth_axes,
    const casacore::Vector<casacore::Int>& kernel_types, const casacore::Vector<casacore::Double>& kernel_widths_pix) {
    return casa::MomentsBase<T>::setSmoothMethod(smooth_axes, kernel_types, kernel_widths_pix);
}

template <class T>
std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> ImageMoments<T>::createMoments(
    casacore::Bool do_temp, const casacore::String& out_file_name, casacore::Bool remove_axis) {
    if (!goodParameterStatus_p) {
        throw casacore::AipsError("Internal status of class is bad.  You have ignored errors");
    }

    // check whether the calculation is cancelled
    if (_stop) {
        return std::vector<std::shared_ptr<casacore::MaskedLattice<T>>>();
    }

    // Find spectral axis use a copy of the coordinate system here since, if the image has multiple beams, "_image" will change and hence a
    // reference to its casacore::CoordinateSystem will disappear causing a seg fault.
    casacore::CoordinateSystem csys = _image->coordinates();
    casacore::Int spectralAxis = csys.spectralAxisNumber(false);
    if (momentAxis_p == momentAxisDefault_p) {
        this->setMomentAxis(spectralAxis); // this step will do 2D convolve for a per plane beam image

        // check whether the calculation is cancelled
        if (_stop) {
            return std::vector<std::shared_ptr<casacore::MaskedLattice<T>>>();
        }

        if (_image->shape()(momentAxis_p) <= 1) {
            goodParameterStatus_p = false;
            throw casacore::AipsError("Illegal moment axis; it has only 1 pixel");
        }
        worldMomentAxis_p = csys.pixelAxisToWorldAxis(momentAxis_p);
    }

    convertToVelocity_p = (momentAxis_p == spectralAxis) && (csys.spectralCoordinate().restFrequency() > 0);

    casacore::String moment_axis_units = csys.worldAxisUnits()(worldMomentAxis_p);
    spdlog::info("Moment axis type is {}.", csys.worldAxisNames()(worldMomentAxis_p));

    // If the moment axis is a spectral axis, indicate we want to convert to velocity. Check the user's requests are allowed
    _checkMethod();

    // Check that input and output image names aren't the same, if there is only one output image
    if (moments_p.nelements() == 1 && !do_temp) {
        if (!out_file_name.empty() && (out_file_name == _image->name())) {
            throw casacore::AipsError("Input image and output image have same name");
        }
    }

    // Set methods
    auto smooth_clip_method = false;
    auto window_method = false;
    auto fit_method = false;
    auto clip_method = false;

    if (doSmooth_p && !doWindow_p) {
        smooth_clip_method = true;
    } else if (doWindow_p) {
        window_method = true;
    } else if (doFit_p) {
        fit_method = true;
    } else {
        clip_method = true;
    }

    // We only smooth the image if we are doing the smooth/clip method or possibly the interactive window method. Note that the convolution
    // routines can only handle convolution when the image fits fully in core at present.
    SPIIT smoothed_image;
    if (doSmooth_p) {
        smoothed_image = SmoothImage();
    }

    // Set output images shape and coordinates.
    casacore::IPosition out_image_shape;
    const auto out_csys = this->_makeOutputCoordinates(out_image_shape, csys, _image->shape(), momentAxis_p, remove_axis);
    auto moments_size = moments_p.nelements();

    // Resize the vector of pointers for output images
    std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> output_images(moments_size);

    // Loop over desired output moments
    casacore::String suffix;
    casacore::Bool good_units;
    casacore::Bool give_message = true;
    const auto image_units = _image->units();

    for (casacore::uInt i = 0; i < moments_size; ++i) {
        // Set moment image units and assign pointer to output moments array Value of goodUnits is the same for each output moment image
        casacore::Unit moment_units;
        good_units = this->_setOutThings(suffix, moment_units, image_units, moment_axis_units, moments_p(i), convertToVelocity_p);

        // Create output image(s). Either casacore::PagedImage or TempImage
        SPIIT output_image;

        if (!do_temp) {
            // Get the name of the original image file
            const casacore::String in = _image->name(false);
            casacore::String out_temp_file_name;

            if (moments_p.size() == 1) {
                if (out_file_name.empty()) {
                    out_temp_file_name = in + suffix;
                } else {
                    out_temp_file_name = out_file_name;
                }
            } else {
                if (out_file_name.empty()) {
                    out_temp_file_name = in + suffix;
                } else {
                    out_temp_file_name = out_file_name + suffix;
                }
            }

            if (!overWriteOutput_p) {
                casacore::NewFile new_file;
                casacore::String error;
                if (!new_file.valueOK(out_temp_file_name, error)) {
                    throw casacore::AipsError(error);
                }
            }
            output_image.reset(new casacore::PagedImage<T>(out_image_shape, out_csys, out_temp_file_name));

        } else {
            output_image.reset(new casacore::TempImage<T>(casacore::TiledShape(out_image_shape), out_csys));
        }

        ThrowIf(!output_image, "Failed to create output file");
        output_image->setMiscInfo(_image->miscInfo());
        output_image->setImageInfo(_image->imageInfo());
        output_image->makeMask("mask0", true, true);

        // Set output image units if possible
        if (good_units) {
            output_image->setUnits(moment_units);
        } else {
            if (give_message) {
                spdlog::warn(
                    "Could not determine the units of the moment image(s). So the units will be the same as those of the input image. This "
                    "may not be very useful.");
                give_message = false;
            }
        }

        output_images[i] = output_image;
    }

    // If the user is using the automatic, non-fitting window method, they need a good assessment of the noise. The user can input that
    // value, but if they don't, we work it out here.
    T noise;
    if (stdDeviation_p <= T(0) && (doWindow_p || (doFit_p && !doWindow_p))) {
        if (smoothed_image) {
            spdlog::info("Evaluating noise level from smoothed image.");
            WhatIsTheNoise(noise, *smoothed_image);
        } else {
            spdlog::info("Evaluating noise level from input image.");
            WhatIsTheNoise(noise, *_image);
        }
        stdDeviation_p = noise;
    }

    // Create appropriate MomentCalculator object
    shared_ptr<casa::MomentCalcBase<T>> moment_calculator;
    if (clip_method || smooth_clip_method) {
        moment_calculator.reset(new casa::MomentClip<T>(smoothed_image, *this, os_p, output_images.size()));

    } else if (window_method) {
        moment_calculator.reset(new casa::MomentWindow<T>(smoothed_image, *this, os_p, output_images.size()));

    } else if (fit_method) {
        moment_calculator.reset(new casa::MomentFit<T>(*this, os_p, output_images.size()));
    }

    // Iterate optimally through the image, compute the moments, fill the output lattices
    casacore::uInt out_images_size = output_images.size();
    casacore::PtrBlock<casacore::MaskedLattice<T>*> ptr_blocks(out_images_size);
    for (casacore::uInt i = 0; i < out_images_size; ++i) {
        ptr_blocks[i] = output_images[i].get();
    }

    // Do expensive calculation
    LineMultiApply(ptr_blocks, *_image, *moment_calculator, momentAxis_p);

    if (window_method || fit_method) {
        if (moment_calculator->nFailedFits() != 0) {
            spdlog::warn("There were {} failed fits.", moment_calculator->nFailedFits());
        }
    }

    if (_stop) {
        // Reset shared ptrs for output moments images if calculation is cancelled
        for (auto& output_image : output_images) {
            output_image.reset();
        }
        // Clear the output image ptrs vector if calculation is cancelled
        output_images.clear();
    } else {
        for (auto& output_image : output_images) {
            output_image->flush();
        }
    }

    return output_images;
}

// casacore::Smooth image. casacore::Input masked pixels are zeros before smoothing. The output smoothed image is masked as well to reflect
// the input mask.
template <class T>
SPIIT ImageMoments<T>::SmoothImage() {
    auto max_axis = max(smoothAxes_p) + 1;
    ThrowIf(max_axis > casacore::Int(_image->ndim()), "You have specified an illegal smoothing axis");

    SPIIT smoothed_image;
    if (smoothOut_p.empty()) {
        smoothed_image.reset(new casacore::TempImage<T>(_image->shape(), _image->coordinates()));
    } else {
        // This image has already been checked in setSmoothOutName to not exist
        smoothed_image.reset(new casacore::PagedImage<T>(_image->shape(), _image->coordinates(), smoothOut_p));
    }

    smoothed_image->setMiscInfo(_image->miscInfo());

    // Do the convolution. Conserve flux.
    casa::SepImageConvolver<T> sep_image_con(*_image, os_p, true);
    auto smooth_axes_size = smoothAxes_p.size();
    for (casacore::uInt i = 0; i < smooth_axes_size; ++i) {
        casacore::VectorKernel::KernelTypes type = casacore::VectorKernel::KernelTypes(kernelTypes_p[i]);
        sep_image_con.setKernel(casacore::uInt(smoothAxes_p[i]), type, kernelWidths_p[i], true, false, 1.0);
    }
    sep_image_con.convolve(*smoothed_image);

    return smoothed_image;
}

// Determine the noise level in the image by first making a histogram of the image, then fitting a Gaussian between the 25% levels to give
// sigma Find a histogram of the image
template <class T>
void ImageMoments<T>::WhatIsTheNoise(T& sigma, const casacore::ImageInterface<T>& image) {
    casa::ImageHistograms<T> hist(image, false);
    const casacore::uInt num_of_bins = 100;
    hist.setNBins(num_of_bins);

    // It is safe to use casacore::Vector rather than casacore::Array because we are binning the whole image and ImageHistograms will only
    // resize these Vectors to a 1-D shape
    casacore::Vector<T> values, counts; // (x, y) for histograms vectors
    ThrowIf(!hist.getHistograms(values, counts), "Unable to make histogram of image");

    // Enter into a plot/fit loop
    auto bin_width = values(1) - values(0);
    T x_min, x_max, y_min, y_max;

    x_min = values(0) - bin_width;
    x_max = values(num_of_bins - 1) + bin_width;
    casacore::Float x_min_f = casacore::Float(real(x_min));
    casacore::Float x_max_f = casacore::Float(real(x_max));
    casacore::LatticeStatsBase::stretchMinMax(x_min_f, x_max_f);

    casacore::IPosition y_min_pos(1), y_max_pos(1);
    casacore::minMax(y_min, y_max, y_min_pos, y_max_pos, counts);
    casacore::Float y_max_f = casacore::Float(real(y_max));
    y_max_f += y_max_f / 20;

    auto first = true;
    auto more = true;

    while (more) {
        casacore::Int index_min = 0;
        casacore::Int index_max = 0;

        if (first) {
            first = false;

            index_max = y_max_pos(0);
            casacore::uInt i;
            for (i = y_max_pos(0); i < num_of_bins; i++) {
                if (counts(i) < y_max / 4) {
                    index_max = i;
                    break;
                }
            }

            index_min = y_min_pos(0);
            for (i = y_max_pos(0); i > 0; i--) {
                if (counts(i) < y_max / 4) {
                    index_min = i;
                    break;
                }
            }

            // Check range is sensible
            if (index_max <= index_min || abs(index_max - index_min) < 3) {
                spdlog::warn("The image histogram is strangely shaped, fitting to all bins.");
                index_min = 0;
                index_max = num_of_bins - 1;
            }
        }

        // Now generate the distribution we want to fit. Normalize to peak 1 to help fitter.
        const casacore::uInt num_of_points = index_max - index_min + 1;
        casacore::Vector<T> data_x(num_of_points);
        casacore::Vector<T> data_y(num_of_points);
        casacore::Int i;

        for (i = index_min; i <= index_max; i++) {
            data_x(i - index_min) = values(i);
            data_y(i - index_min) = counts(i) / y_max;
        }

        // Create a fitter
        casacore::NonLinearFitLM<T> fitter;
        casacore::Gaussian1D<casacore::AutoDiff<T>> gauss;
        fitter.setFunction(gauss);

        // Guess initial fit parameters
        casacore::Vector<T> v(3);
        v(0) = 1.0;                           // height
        v(1) = values(y_max_pos(0));          // position
        v(2) = num_of_points * bin_width / 2; // width

        // Fit
        fitter.setParameterValues(v);
        fitter.setMaxIter(50);
        T criteria = 0.001;
        fitter.setCriteria(criteria);
        casacore::Vector<T> result_sigma(num_of_points);
        result_sigma = 1;
        casacore::Vector<T> solution;
        casacore::Bool fail = false;

        try {
            solution = fitter.fit(data_x, data_y, result_sigma);
        } catch (const casacore::AipsError& x) {
            fail = true;
        }

        // Return values of fit
        if (!fail && fitter.converged()) {
            sigma = T(abs(solution(2)) / casacore::C::sqrt2);
            spdlog::info("The fitted standard deviation of the noise is {}.", sigma);
        } else {
            spdlog::warn("The fit to determine the noise level failed. Try inputting it directly.");
        }

        // Another go
        more = false;
    }
}

template <class T>
void ImageMoments<T>::StopCalculation() {
    _stop = true;
    if (_image_2d_convolver) {
        _image_2d_convolver->StopCalculation();
    }
}

template <class T>
void ImageMoments<T>::LineMultiApply(casacore::PtrBlock<casacore::MaskedLattice<T>*>& lattice_out,
    const casacore::MaskedLattice<T>& lattice_in, casacore::LineCollapser<T, T>& collapser, casacore::uInt collapse_axis) {
    // First verify that all the output lattices have the same shape and tile shape
    const casacore::uInt n_out = lattice_out.nelements(); // Number of output lattices
    AlwaysAssert(n_out > 0, AipsError);

    const casacore::IPosition out_shape(lattice_out[0]->shape());
    const casacore::uInt out_dim = out_shape.nelements();
    for (casacore::uInt i = 1; i < n_out; ++i) {
        AlwaysAssert(lattice_out[i]->shape() == out_shape, AipsError);
    }

    const casacore::IPosition& in_shape = lattice_in.shape();
    casacore::IPosition out_pos(out_dim, 0);

    // Does the input has a mask? If not, can the collapser handle a null mask.
    casacore::Bool use_mask = lattice_in.isMasked() ? casacore::True : (!collapser.canHandleNullMask());
    const casacore::uInt in_ndim = in_shape.size();
    const casacore::IPosition display_axes = IPosition::makeAxisPath(in_ndim).otherAxes(in_ndim, IPosition(1, collapse_axis));
    const casacore::uInt n_display_axes = display_axes.size();

    casacore::Vector<T> result(n_out);                   // Resulting values for a slice
    casacore::Vector<casacore::Bool> result_mask(n_out); // Resulting masks for a slice

    // Read in larger chunks than before, because that was very inefficient and brought NRAO cluster to a snail's pace, and then do the
    // accounting for the input lines in memory
    casacore::IPosition chunk_slice_start(in_ndim, 0);
    casacore::IPosition chunk_slice_end = chunk_slice_start;
    chunk_slice_end[collapse_axis] = in_shape[collapse_axis] - 1;                    // Position at the end of a collapse axis line
    const casacore::IPosition chunk_slice_end_at_chunk_iter_begin = chunk_slice_end; // As an increment of a chunk for the lattice iterator

    // Get a chunk shape and used it to set the data iterator

    casacore::IPosition chunk_shape_init = ChunkShape(collapse_axis, lattice_in);

    casacore::IPosition hdf5_chunk_shape(in_ndim, 1);
    hdf5_chunk_shape[0] = 512;
    hdf5_chunk_shape[1] = 512;

    auto nice_shape = lattice_in.niceCursorShape();
    if (nice_shape == hdf5_chunk_shape) {
        chunk_shape_init[0] = nice_shape[0];
        chunk_shape_init[1] = nice_shape[1];
    }

    casacore::LatticeStepper my_stepper(in_shape, chunk_shape_init, LatticeStepper::RESIZE);
    casacore::RO_MaskedLatticeIterator<T> lat_iter(lattice_in, my_stepper);

    casacore::IPosition cur_pos;                           // Current position for the chunk iterator
    static const casacore::Vector<casacore::Bool> no_mask; // False mask vector

    if (_progress_monitor && (_steps_for_beam_convolution == 0)) { // no beam convolution done before, so initialize the progress meter
        casacore::uInt total_slices = in_shape.product() / in_shape[collapse_axis];
        _progress_monitor->init(total_slices);
    }

    casacore::uInt n_done = 0; // Number of slices have done

    // Iterate through a cube image, chunk by chunk
    for (lat_iter.reset(); !lat_iter.atEnd(); ++lat_iter) {
        const casacore::IPosition iter_pos = lat_iter.position();
        const casacore::Array<T>& chunk = lat_iter.cursor();
        casacore::IPosition chunk_shape = chunk.shape();
        const casacore::Array<casacore::Bool> mask_chunk = use_mask ? lat_iter.getMask() : Array<Bool>();

        chunk_slice_start = 0;
        chunk_slice_end = chunk_slice_end_at_chunk_iter_begin;
        casacore::IPosition result_array_shape = chunk_shape;
        result_array_shape[collapse_axis] = 1;
        std::vector<casacore::Array<T>> result_arrays(n_out);                   // Resulting value arrays for a chunk
        std::vector<casacore::Array<casacore::Bool>> result_array_masks(n_out); // Resulting mask arrays for a chunk

        // Need to initialize this way rather than doing it in the constructor, because using a single Array in the constructor means that
        // all Arrays in the vector reference the same Array.
        for (casacore::uInt k = 0; k < n_out; k++) {
            result_arrays[k] = Array<T>(result_array_shape);
            result_array_masks[k] = Array<Bool>(result_array_shape);
        }

        // Iterate through a chunk, slice by slice on the output image display axes
        casacore::Bool done = casacore::False;
        while (!done) {
            if (_stop) { // Break the iteration in a chunk
                break;
            }

            casacore::Vector<T> data(chunk(chunk_slice_start, chunk_slice_end));
            casacore::Vector<Bool> mask =
                use_mask ? casacore::Vector<casacore::Bool>(mask_chunk(chunk_slice_start, chunk_slice_end)) : no_mask;
            cur_pos = iter_pos + chunk_slice_start;

            // Do calculations
            collapser.multiProcess(result, result_mask, data, mask, cur_pos);

            // Fill partial results in a chunk
            for (uInt k = 0; k < n_out; ++k) {
                result_arrays[k](chunk_slice_start) = result[k];
                result_array_masks[k](chunk_slice_start) = result_mask[k];
            }

            done = True; // The scan of this chunk is complete

            // Report the number of slices have done
            if (_progress_monitor) {
                ++n_done;
                _progress_monitor->nstepsDone(n_done + _steps_for_beam_convolution);
            }

            // Proceed to the next slice on the display axes
            for (casacore::uInt k = 0; k < n_display_axes; ++k) {
                casacore::uInt dax = display_axes[k];
                if (chunk_slice_start[dax] < chunk_shape[dax] - 1) {
                    ++chunk_slice_start[dax];
                    ++chunk_slice_end[dax];
                    done = False;
                    break;
                } else {
                    chunk_slice_start[dax] = 0;
                    chunk_slice_end[dax] = 0;
                }
            }
        }

        if (_stop) { // Break the iteration in a cube image
            break;
        }

        // Put partial results in the output lattices (as a chunk size)
        for (casacore::uInt k = 0; k < n_out; ++k) {
            casacore::IPosition result_pos = in_ndim == out_dim ? iter_pos : iter_pos.removeAxes(casacore::IPosition(1, collapse_axis));
            casacore::Bool keep_axis = result_arrays[k].ndim() == lattice_out[k]->ndim();
            if (!keep_axis) {
                result_arrays[k].removeDegenerate(display_axes);
            }
            lattice_out[k]->putSlice(result_arrays[k], result_pos);

            if (lattice_out[k]->hasPixelMask()) {
                casacore::Lattice<casacore::Bool>& mask_out = lattice_out[k]->pixelMask();
                if (mask_out.isWritable()) {
                    if (!keep_axis) {
                        result_array_masks[k].removeDegenerate(display_axes);
                    }
                    mask_out.putSlice(result_array_masks[k], result_pos);
                }
            }
        }
    }

    if (_progress_monitor) {
        _progress_monitor->done();
    }
}

template <class T>
casacore::IPosition ImageMoments<T>::ChunkShape(casacore::uInt axis, const casacore::MaskedLattice<T>& lattice_in) {
    casacore::uInt ndim = lattice_in.ndim();
    casacore::IPosition chunk_shape(ndim, 1);
    casacore::IPosition lat_in_shape = lattice_in.shape();
    casacore::uInt axis_length = lat_in_shape[axis];
    chunk_shape[axis] = axis_length;

    // arbitrary, but reasonable, max memory limit in bytes for storing arrays in bytes
    static const casacore::uInt limit = 2e7; // Set the limit of memory usage (Bytes)
    static const casacore::uInt size_of_T = sizeof(T);
    static const casacore::uInt size_of_Bool = sizeof(casacore::Bool);
    casacore::uInt chunk_mult = lattice_in.isMasked() ? size_of_T + size_of_Bool : size_of_T;
    casacore::uInt sub_chunk_size = chunk_mult * axis_length; // Memory size for a sub-chunk (Bytes), i.e., memory for a specific axis line

    // integer division
    const casacore::uInt chunk_size = limit / sub_chunk_size; // Chunk size, i.e., number of pixels on display axes
    if (chunk_size <= 1) {
        // can only go row by row
        return chunk_shape;
    }

    ssize_t x = chunk_size;
    for (casacore::uInt i = 0; i < ndim; ++i) {
        if (i != axis) {
            chunk_shape[i] = std::min(x, lat_in_shape[i]);
            // integer division
            x /= chunk_shape[i];
            if (x == 0) {
                break;
            }
        }
    }
    return chunk_shape;
}

#endif // CARTA_BACKEND__MOMENT_IMAGEMOMENTS_TCC_
