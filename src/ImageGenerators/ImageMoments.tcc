/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/ImageMoments.tcc"
//
#ifndef CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_TCC_
#define CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_TCC_

#include <algorithm>
#include <memory>
#include <vector>

#include <omp.h>
#include <chrono>
#include <cmath>
#include <vector>

#include <casacore/casa/OS/HostInfo.h>

#include "../Logger/Logger.h"
#include "Util/Casacore.h"

using namespace carta;

namespace carta {

// Where the moment walk's time goes.
//
// Both halves of it are parallel and they came out about even, so the split is worth keeping
// visible. Timed per slab rather than per line, so the cost of the timing does not land on the
// thing being timed: a per-line version of this cost 30% on a cube with 36.8 million lines.
//
// Begin and Since are explicit rather than a scoped guard, because what a phase produces -- the
// slab, the results collapsed out of it -- outlives the phase and cannot be shut inside a block,
// and because the setup between two phases is deliberately counted in neither of them.
class WalkTiming {
public:
    double read = 0.0;
    double collapse = 0.0;
    double store = 0.0;
    std::size_t slabs = 0;
    std::size_t lines = 0;

    void Begin() {
        _phase = Clock::now();
    }

    double Since() const {
        return std::chrono::duration<double>(Clock::now() - _phase).count();
    }

    void Log(std::size_t workers) const {
        const double total = std::chrono::duration<double>(Clock::now() - _started).count();
        spdlog::debug(
            "moment walk: {:.1f} s over {} slabs and {} lines -- read {:.1f} s ({:.0f}%, decode pool), "
            "collapsing {:.1f} s ({:.0f}%, {} workers), writing {:.1f} s ({:.0f}%)",
            total, slabs, lines, read, 100.0 * read / total, collapse, 100.0 * collapse / total, workers, store,
            100.0 * store / total);
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point _started = Clock::now();
    Clock::time_point _phase = Clock::now();
};

} // namespace carta

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
    casacore::DataType imageType = casacore::whatType<T>();
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
            // Held in memory now, not in the store: the chunks are no longer what it decodes in.
            _chunk_grid_unit = casacore::IPosition();
            _chunk_grid_origin = casacore::IPosition();
            _hold_cache = nullptr;
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

    // Create appropriate MomentCalculator objects, one per worker.
    //
    // Each holds its own scratch, including a whole copy of the coordinate system -- taken by
    // value in setCoordinateSystem -- which is what makes the toWorld call inside multiProcess
    // safe to run on several threads at once. They read *this while constructing, so they are
    // built here, before anything is parallel; multiProcess only ever takes it as const.
    // Five of the moment types turn a pixel on the collapse axis into a world coordinate, and they
    // do it for every line. casacore's conversion is not reentrant even when each worker holds its
    // own CoordinateSystem -- setCoordinateSystem copies one per collapser, and it still is not
    // enough. Measured: asking for all twelve moments, the parallel walk disagrees with
    // casa::ImageMoments every run; asking for AVERAGE alone it agrees over repeated runs, and
    // serialising multiProcess alone also makes the twelve agree.
    //
    // So the walk runs on one worker whenever one of the five is asked for. This is exactly
    // doCoordCalc's own test (MomentCalcBase.tcc:133), which keys on the requested moments and
    // nothing else, so it cannot disagree with what the collapser will actually do.
    bool needs_world_coordinates = false;
    for (casacore::uInt i = 0; i < moments_p.nelements(); ++i) {
        const casacore::Int moment = moments_p(i);
        if (moment == casa::MomentsBase<T>::WEIGHTED_MEAN_COORDINATE ||
            moment == casa::MomentsBase<T>::WEIGHTED_DISPERSION_COORDINATE ||
            moment == casa::MomentsBase<T>::MEDIAN_COORDINATE ||
            moment == casa::MomentsBase<T>::MAXIMUM_COORDINATE ||
            moment == casa::MomentsBase<T>::MINIMUM_COORDINATE) {
            needs_world_coordinates = true;
            break;
        }
    }
    //
    // And only a clipping collapser runs on more than one, because MomentClip is the one whose
    // multiProcess has been read and measured for it. MomentWindow's keeps the window it finds for a
    // line in two function-local statics (MomentWindow.tcc:167), which every collapser shares
    // whatever instance it is, so two workers overwrite each other's window mid-line. MomentFit has
    // not been shown to be safe either. CARTA asks for neither today -- nothing calls
    // setWinFitMethod -- which is why this is a guard rather than a fix anyone has seen.
    const bool clipping = clip_method || smooth_clip_method;
    const std::size_t workers = (needs_world_coordinates || !clipping) ? 1 : std::max(1, omp_get_max_threads());
    if (needs_world_coordinates) {
        spdlog::debug("moment walk: one worker, because a coordinate moment was asked for");
    } else if (!clipping) {
        spdlog::debug("moment walk: one worker, because the window and fit methods are not safe to run in parallel");
    }
    std::vector<std::shared_ptr<casa::MomentCalcBase<T>>> moment_calculators(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        if (clip_method || smooth_clip_method) {
            moment_calculators[worker].reset(new casa::MomentClip<T>(smoothed_image, *this, os_p, output_images.size()));

        } else if (window_method) {
            moment_calculators[worker].reset(new casa::MomentWindow<T>(smoothed_image, *this, os_p, output_images.size()));

        } else if (fit_method) {
            moment_calculators[worker].reset(new casa::MomentFit<T>(*this, os_p, output_images.size()));
        }
    }

    // Iterate optimally through the image, compute the moments, fill the output lattices
    casacore::uInt out_images_size = output_images.size();
    casacore::PtrBlock<casacore::MaskedLattice<T>*> ptr_blocks(out_images_size);
    for (casacore::uInt i = 0; i < out_images_size; ++i) {
        ptr_blocks[i] = output_images[i].get();
    }

    // Do expensive calculation
    LineMultiApply(ptr_blocks, *_image, moment_calculators, momentAxis_p);

    if (window_method || fit_method) {
        casacore::uInt failed_fits = 0;
        for (const auto& calculator : moment_calculators) {
            failed_fits += calculator->nFailedFits();
        }
        if (failed_fits != 0) {
            spdlog::warn("There were {} failed fits.", failed_fits);
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
            sigma = T(abs(solution(2)) / M_SQRT2);
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
    const casacore::MaskedLattice<T>& lattice_in,
    const std::vector<std::shared_ptr<casa::MomentCalcBase<T>>>& collapsers, casacore::uInt collapse_axis) {
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
    casacore::Bool use_mask = lattice_in.isMasked() ? casacore::True : (!collapsers.front()->canHandleNullMask());
    const casacore::uInt in_ndim = in_shape.size();
    const casacore::IPosition display_axes = IPosition::makeAxisPath(in_ndim).otherAxes(in_ndim, IPosition(1, collapse_axis));
    const casacore::uInt n_display_axes = display_axes.size();

    // One set of these per worker, for the same reason there is one collapser per worker.
    const std::size_t workers = collapsers.size();
    std::vector<casacore::Vector<T>> results(workers);
    std::vector<casacore::Vector<casacore::Bool>> result_masks(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        // Assigned from a fresh temporary each, not resized in place. A casacore Array copy is a
        // reference, so filling a vector with copies of one Array leaves every element sharing the
        // same storage -- the trap the comment on result_arrays below is about. Here it would have
        // every worker writing its result over every other worker's.
        results[worker] = casacore::Vector<T>(n_out);
        result_masks[worker] = casacore::Vector<casacore::Bool>(n_out);
    }

    // Read in larger chunks than before, because that was very inefficient and brought NRAO cluster to a snail's pace, and then do the
    // accounting for the input lines in memory
    casacore::IPosition chunk_slice_start(in_ndim, 0);
    casacore::IPosition chunk_slice_end = chunk_slice_start;
    chunk_slice_end[collapse_axis] = in_shape[collapse_axis] - 1;                    // Position at the end of a collapse axis line
    const casacore::IPosition chunk_slice_end_at_chunk_iter_begin = chunk_slice_end; // As an increment of a chunk for the lattice iterator

    // The slab to step through the image in. What the image decodes together is the chunk grid the
    // caller gave, while it is still this image's; otherwise its cursor advice, which is the only
    // thing a lattice says about its chunking.
    const casacore::uInt pixel_bytes = lattice_in.isMasked() ? sizeof(T) + sizeof(casacore::Bool) : sizeof(T);
    const ptrdiff_t total_kib = casacore::HostInfo::memoryTotal();
    const std::uint64_t memory_bytes = total_kib > 0 ? static_cast<std::uint64_t>(total_kib) * 1024 : 0;
    const bool on_the_grid = _chunk_grid_unit.size() == in_ndim && _chunk_grid_origin.size() == in_ndim;
    const SlabPlan plan = on_the_grid ? PlanSlab(in_shape, _chunk_grid_unit, collapse_axis, pixel_bytes, memory_bytes, _chunk_grid_origin)
                                      : PlanSlab(in_shape, lattice_in.niceCursorShape(), collapse_axis, pixel_bytes, memory_bytes);
    // Kept until the walk is done: what its slabs share, held where they can find it again.
    std::shared_ptr<void> held_cache;
    const std::uint64_t cache_bytes = on_the_grid && _hold_cache ? plan.CacheBytes(sizeof(T)) : 0;
    if (on_the_grid && _hold_cache) {
        held_cache = _hold_cache(cache_bytes);
    }
    if (plan.chunked) {
        // Worth saying out loud: every chunk is decoded once per slab that lands in it, short of a cache
        // that keeps it for the next.
        spdlog::debug("moment slab {} path {} from the {}, budget {} MiB, unit {} MiB, reads the store {:.1f}x, cache {} MiB{}{}",
            plan.slab.toString(), plan.axis_path.toString(), on_the_grid ? "chunks" : "cursor advice", plan.budget_bytes >> 20U,
            plan.unit_bytes >> 20U, plan.store_reads, cache_bytes >> 20U, held_cache ? " of its own" : "",
            held_cache ? fmt::format(" ({}{:.1f}x with it)", cache_bytes < plan.reuse_pixels * sizeof(T) ? "short of the need; " : "",
                             plan.cached_store_reads)
                       : std::string());
    }

    casacore::LatticeStepper my_stepper(in_shape, plan.slab, plan.axis_path, LatticeStepper::RESIZE);
    casacore::RO_MaskedLatticeIterator<T> lat_iter(lattice_in, my_stepper);

    static const casacore::Vector<casacore::Bool> no_mask; // False mask vector

    if (_progress_monitor && (_steps_for_beam_convolution == 0)) { // no beam convolution done before, so initialize the progress meter
        casacore::uInt total_slices = in_shape.product() / in_shape[collapse_axis];
        _progress_monitor->init(total_slices);
    }

    casacore::uInt n_done = 0; // Number of slices have done

    WalkTiming timing;

    // Iterate through a cube image, chunk by chunk
    for (lat_iter.reset(); !lat_iter.atEnd();) {
        timing.Begin();
        const casacore::IPosition iter_pos = lat_iter.position();
        const casacore::Array<T>& chunk = lat_iter.cursor();
        casacore::IPosition chunk_shape = chunk.shape();
        const casacore::Array<casacore::Bool> mask_chunk = use_mask ? lat_iter.getMask() : Array<Bool>();
        timing.read += timing.Since();
        ++timing.slabs;

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

        // Every line in this slab, one per position on the display axes. The odometer this
        // replaces walked them in the same order; a flat index is what lets the loop be split,
        // and the order does not matter because each line writes one element nothing else
        // touches. No reduction across lines, so the answer is the same to the last bit.
        std::size_t slab_lines = 1;
        for (casacore::uInt k = 0; k < n_display_axes; ++k) {
            slab_lines *= static_cast<std::size_t>(chunk_shape[display_axes[k]]);
        }

        // Write through raw pointers rather than Array::operator()(IPosition): the elements are
        // disjoint either way, but this does not lean on that operator being reentrant.
        std::vector<T*> value_out(n_out);
        std::vector<casacore::Bool*> mask_out_ptr(n_out);
        for (casacore::uInt k = 0; k < n_out; ++k) {
            value_out[k] = result_arrays[k].data();
            mask_out_ptr[k] = result_array_masks[k].data();
        }
        std::vector<std::size_t> result_stride(in_ndim, 0);
        std::size_t stride = 1;
        for (casacore::uInt axis = 0; axis < in_ndim; ++axis) {
            result_stride[axis] = stride;
            stride *= static_cast<std::size_t>(result_array_shape[axis]);
        }

        timing.Begin();
#pragma omp parallel for schedule(static) num_threads(static_cast<int>(workers))
        for (std::ptrdiff_t line = 0; line < static_cast<std::ptrdiff_t>(slab_lines); ++line) {
            if (_stop) { // Cannot break out of a parallel loop; skipping is the same thing here
                continue;
            }
            const std::size_t worker = static_cast<std::size_t>(omp_get_thread_num());

            // Unpack the flat index onto the display axes, fastest first.
            casacore::IPosition slice_start(in_ndim, 0);
            std::size_t rest = static_cast<std::size_t>(line);
            for (casacore::uInt k = 0; k < n_display_axes; ++k) {
                const casacore::uInt dax = display_axes[k];
                const std::size_t extent = static_cast<std::size_t>(chunk_shape[dax]);
                slice_start[dax] = static_cast<ssize_t>(rest % extent);
                rest /= extent;
            }
            casacore::IPosition slice_end = slice_start;
            slice_end[collapse_axis] = chunk_slice_end_at_chunk_iter_begin[collapse_axis];

            casacore::Vector<T> data(chunk(slice_start, slice_end));
            casacore::Vector<Bool> mask =
                use_mask ? casacore::Vector<casacore::Bool>(mask_chunk(slice_start, slice_end)) : no_mask;
            const casacore::IPosition cur_pos = iter_pos + slice_start;

            // Do calculations

            collapsers[worker]->multiProcess(results[worker], result_masks[worker], data, mask, cur_pos);

            // Fill partial results in a chunk
            std::size_t offset = 0;
            for (casacore::uInt axis = 0; axis < in_ndim; ++axis) {
                offset += static_cast<std::size_t>(slice_start[axis]) * result_stride[axis];
            }
            for (uInt k = 0; k < n_out; ++k) {
                value_out[k][offset] = results[worker][k];
                mask_out_ptr[k][offset] = result_masks[worker][k];
            }
        }
        timing.lines += slab_lines;

        // Report progress once for the slab. Per line it was a virtual call on every one of them,
        // and from inside a parallel loop it would need serialising as well.
        if (_progress_monitor) {
            n_done += static_cast<casacore::uInt>(slab_lines);
            _progress_monitor->nstepsDone(n_done + _steps_for_beam_convolution);
        }

        timing.collapse += timing.Since();
        timing.Begin();

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

        timing.store += timing.Since();
        ++lat_iter;
    }

    timing.Log(collapsers.size());

    if (_progress_monitor) {
        _progress_monitor->done();
    }
}

#endif // CARTA_SRC_IMAGEGENERATORS_IMAGEMOMENTS_TCC_
