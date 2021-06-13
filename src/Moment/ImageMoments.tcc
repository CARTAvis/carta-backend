/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/ImageMoments.tcc"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGEMOMENTS_TCC_
#define CARTA_BACKEND__MOMENT_IMAGEMOMENTS_TCC_

#include "../Logger/Logger.h"
#include "../Util.h"

static const casacore::uInt MAX_MEMORY = 1e8; // maximum memory usage for the chunk data (Bytes)

using namespace carta;

template <class T>
ImageMoments<T>::ImageMoments(
    const casacore::ImageInterface<T>& image, casacore::LogIO& os, casa::ImageMomentsProgressMonitor* progress_monitor)
    : casa::MomentsBase<T>(os, true, true), _stop(false), _image_2d_convolver(nullptr), _progress_monitor(nullptr) {
    _image.reset(image.cloneII());
    if (progress_monitor) { // set the progress meter
        _progress_monitor = std::make_unique<casa::ImageMomentsProgress>();
        _progress_monitor->setProgressMonitor(progress_monitor);
    }
}

template <class T>
casacore::Bool ImageMoments<T>::setMomentAxis(const casacore::Int moment_axis) {
    // reset the number of steps have done for the beam convolution
    _steps_for_beam_convolution = 0;

    momentAxis_p = moment_axis;
    if (momentAxis_p < 0) {
        momentAxis_p = _image->coordinates().spectralAxisNumber();
        if (momentAxis_p == -1) {
            throw casacore::AipsError("There is no spectral axis in this image; specify the axis");
        }
    } else {
        if (momentAxis_p < 0 || momentAxis_p > casacore::Int(_image->ndim() - 1)) {
            throw casacore::AipsError("Illegal moment axis; out of range");
        }
        if (_image->shape()(momentAxis_p) <= 0) {
            throw casacore::AipsError("Illegal moment axis; it has no pixels");
        }
    }

    if (momentAxis_p == _image->coordinates().spectralAxisNumber() && _image->imageInfo().hasMultipleBeams()) {
        // set parameters for the image 2D convolver
        auto dir_axes = _image->coordinates().directionAxesNumbers();
        casacore::GaussianBeam max_beam = casa::CasaImageBeamSet(_image->imageInfo().getBeamSet()).getCommonBeam();
        spdlog::info(
            "The input image has multiple beams so each plane will be convolved to the largest beam size {} prior to calculating moments.",
            GetGaussianInfo(max_beam));

        // reset the image 2D convolver
        _image_2d_convolver.reset(
            new carta::Image2DConvolver<float>(_image, std::make_pair(dir_axes[0], dir_axes[1]), max_beam, _progress_monitor.get()));
        auto image_copy = _image_2d_convolver->DoConvolve();                // do long calculation
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
std::vector<std::shared_ptr<casacore::MaskedLattice<T>>> ImageMoments<T>::createMoments(
    const casacore::String& out_file_name, casacore::Bool remove_axis) {
    // check whether the calculation is cancelled
    if (_stop) {
        return std::vector<std::shared_ptr<casacore::MaskedLattice<T>>>();
    }

    // Find spectral axis use a copy of the coordinate system here since, if the image has multiple beams, "_image" will change and hence a
    // reference to its casacore::CoordinateSystem will disappear causing a seg fault.
    casacore::CoordinateSystem csys = _image->coordinates();
    casacore::Int spectral_axis = csys.spectralAxisNumber(false);

    convertToVelocity_p = (momentAxis_p == spectral_axis) && (csys.spectralCoordinate().restFrequency() > 0);

    casacore::String moment_axis_units = csys.worldAxisUnits()(worldMomentAxis_p);
    spdlog::info("Moment axis type is {}.", csys.worldAxisNames()(worldMomentAxis_p));

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

        // Create output image(s)
        SPIIT output_image = std::make_shared<casacore::TempImage<T>>(casacore::TiledShape(out_image_shape), out_csys);

        ThrowIf(!output_image, "Failed to create output file");
        output_image->setMiscInfo(_image->miscInfo());
        output_image->setImageInfo(_image->imageInfo());
        output_image->appendLog(_image->logger());
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

    // Create appropriate MomentCalculator object
    std::shared_ptr<casa::MomentCalcBase<T>> moment_calculator =
        std::make_shared<casa::MomentClip<T>>(nullptr, *this, os_p, output_images.size());

    // Iterate optimally through the image, compute the moments, fill the output lattices
    casacore::uInt out_images_size = output_images.size();
    casacore::PtrBlock<casacore::MaskedLattice<T>*> ptr_blocks(out_images_size);
    for (casacore::uInt i = 0; i < out_images_size; ++i) {
        ptr_blocks[i] = output_images[i].get();
    }

    // Do expensive calculation
    LineMultiApply(ptr_blocks, *_image, *moment_calculator, momentAxis_p);

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
    const casacore::uInt moments = lattice_out.nelements(); // moments types
    AlwaysAssert(moments > 0, AipsError);

    const casacore::IPosition out_shape(lattice_out[0]->shape());
    const casacore::uInt out_dim = out_shape.nelements();
    for (casacore::uInt i = 1; i < moments; ++i) {
        AlwaysAssert(lattice_out[i]->shape() == out_shape, AipsError);
    }

    const casacore::IPosition& in_shape = lattice_in.shape();
    casacore::IPosition out_pos(out_dim, 0);

    // Does the input has a mask? If not, can the collapser handle a null mask.
    casacore::Bool use_mask = lattice_in.isMasked() ? casacore::True : (!collapser.canHandleNullMask());
    const casacore::uInt in_ndim = in_shape.size();
    const casacore::IPosition display_axes = IPosition::makeAxisPath(in_ndim).otherAxes(in_ndim, IPosition(1, collapse_axis));

    casacore::Vector<T> result(moments);                   // Resulting values for a slice
    casacore::Vector<casacore::Bool> result_mask(moments); // Resulting masks for a slice

    // Read in larger chunks than before, because that was very inefficient and brought NRAO cluster to a snail's pace, and then do the
    // accounting for the input lines in memory
    casacore::IPosition chunk_slice_start(in_ndim, 0);
    casacore::IPosition chunk_slice_end = chunk_slice_start;
    chunk_slice_end[collapse_axis] = in_shape[collapse_axis] - 1;                    // Position at the end of a collapse axis line
    const casacore::IPosition chunk_slice_end_at_chunk_iter_begin = chunk_slice_end; // As an increment of a chunk for the lattice iterator

    // Get a chunk shape and used it to set the data iterator
    casacore::IPosition chunk_shape_init = ChunkShape(collapse_axis, lattice_in);
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
        std::vector<casacore::Array<T>> result_arrays(moments);                   // Resulting value arrays for a chunk
        std::vector<casacore::Array<casacore::Bool>> result_array_masks(moments); // Resulting mask arrays for a chunk

        // Need to initialize this way rather than doing it in the constructor, because using a single Array in the constructor means that
        // all Arrays in the vector reference the same Array.
        for (casacore::uInt k = 0; k < moments; k++) {
            result_arrays[k] = casacore::Array<T>(result_array_shape);
            result_array_masks[k] = casacore::Array<casacore::Bool>(result_array_shape);
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
            for (uInt k = 0; k < moments; ++k) {
                result_arrays[k](chunk_slice_start) = result[k];
                result_array_masks[k](chunk_slice_start) = result_mask[k];
            }

            done = casacore::True; // The scan of this chunk is complete

            // Report the number of slices have done
            if (_progress_monitor) {
                ++n_done;
                _progress_monitor->nstepsDone(n_done + _steps_for_beam_convolution);
            }

            // Proceed to the next slice on the display axes
            for (casacore::uInt k = 0; k < display_axes.size(); ++k) {
                casacore::uInt dax = display_axes[k];
                if (chunk_slice_start[dax] < chunk_shape[dax] - 1) {
                    ++chunk_slice_start[dax];
                    ++chunk_slice_end[dax];
                    done = casacore::False;
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
        for (casacore::uInt k = 0; k < moments; ++k) {
            casacore::IPosition result_pos = (in_ndim == out_dim) ? iter_pos : iter_pos.removeAxes(casacore::IPosition(1, collapse_axis));
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

    // maximum memory limit in bytes for storing arrays
    static const casacore::uInt size_of_T = sizeof(T);
    static const casacore::uInt size_of_Bool = sizeof(casacore::Bool);
    casacore::uInt chunk_mult = lattice_in.isMasked() ? size_of_T + size_of_Bool : size_of_T;
    casacore::uInt sub_chunk_size = chunk_mult * axis_length; // Memory size for a sub-chunk (Bytes), i.e., memory for a specific axis line

    // integer division
    const casacore::uInt chunk_size = MAX_MEMORY / sub_chunk_size; // Chunk size, i.e., number of pixels on display axes
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
