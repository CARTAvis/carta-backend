/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Re-write from the file: "carta-casacore/casa6/casa5/code/imageanalysis/ImageAnalysis/Image2DConvolver.tcc"
//
#ifndef CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_TCC_
#define CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_TCC_

#include "Logger/Logger.h"
#include "Util.h"

using namespace carta;

template <class T>
Image2DConvolver<T>::Image2DConvolver(const SPCIIT image, const std::pair<casacore::uInt, casacore::uInt>& axes,
    const casacore::GaussianBeam& max_beam, casa::ImageMomentsProgress* progress_monitor)
    : _image(image),
      _major(),
      _minor(),
      _pa(),
      _axes(image->coordinates().directionAxesNumbers()),
      _stop(false),
      _progress_monitor(progress_monitor) {
    SetAxes(axes);
    SetKernel(max_beam.getMajor(), max_beam.getMinor(), max_beam.getPA(true));
}

template <class T>
void Image2DConvolver<T>::SetAxes(const std::pair<casacore::uInt, casacore::uInt>& axes) {
    casacore::uInt ndim = _image->ndim();

    ThrowIf(axes.first == axes.second, "Axes must be different");
    ThrowIf(axes.first >= ndim || axes.second >= ndim, "Axis value must be less than number of axes in image");

    if (_axes.size() != 2) {
        _axes.resize(2, false);
    }
    _axes[0] = axes.first;
    _axes[1] = axes.second;
}

template <class T>
void Image2DConvolver<T>::SetKernel(const casacore::Quantity& major, const casacore::Quantity& minor, const casacore::Quantity& pa) {
    ThrowIf(major < minor, "Major axis is less than minor axis");
    _major = major;
    _minor = minor;
    _pa = pa;
}

template <class T>
SPIIT Image2DConvolver<T>::DoConvolve() {
    _stop = false; // reset the flag for cancellation

    ThrowIf(_axes.nelements() != 2, "You must give two pixel axes to DoConvolve");

    auto inc = _image->coordinates().increment();
    auto units = _image->coordinates().worldAxisUnits();

    ThrowIf(
        !casacore::near(casacore::Quantity(fabs(inc[_axes[0]]), units[_axes[0]]), casacore::Quantity(fabs(inc[_axes[1]]), units[_axes[1]])),
        "Pixels must be square, please repair your image so that they are");

    casacore::String empty_out_file;
    casacore::Record region_record;
    casacore::String empty_mask;
    casacore::Bool drop_degenerate_axes(false);
    casacore::Bool overwrite(false);
    casacore::Bool list(false);
    casacore::Bool extend_mask(false);
    auto sub_image = casa::SubImageFactory<T>::createImage(
        *_image, empty_out_file, region_record, empty_mask, drop_degenerate_axes, overwrite, list, extend_mask);

    const casacore::Int ndim = sub_image->ndim();

    ThrowIf(_axes(0) < 0 || _axes(0) >= ndim || _axes(1) < 0 || _axes(1) >= ndim,
        "The pixel axes " + casacore::String::toString(_axes) + " are illegal");
    ThrowIf(ndim < 2, "The image axes must have at least 2 pixel axes");

    std::shared_ptr<TempImage<T>> out_image(new casacore::TempImage<T>(sub_image->shape(), sub_image->coordinates()));

    Convolve(out_image, *sub_image);

    if (sub_image->isMasked()) {
        casacore::TempLattice<Bool> mask(out_image->shape());
        CopyMask(mask, *sub_image);
        out_image->attachMask(mask);
    }

    return PrepareOutputImage(*out_image);
}

template <class T>
void Image2DConvolver<T>::Convolve(SPIIT image_out, const casacore::ImageInterface<T>& image_in) const {
    const auto& in_shape = image_in.shape();
    const auto& out_shape = image_out->shape();
    ThrowIf(!in_shape.isEqual(out_shape), "Input and output images must have the same shape");

    // generate kernel array (height unity)
    casacore::Array<casacore::Double> kernel;

    // initialize to avoid compiler warning, kernel_volume will always be set to something reasonable below before it is used.
    casacore::Double kernel_volume = -1;
    std::vector<casacore::Quantity> original_params{_major, _minor, _pa};

    const auto& csys = image_in.coordinates();
    if (_major.getUnit().startsWith("pix")) {
        auto inc = csys.increment()[_axes[0]];
        auto unit = csys.worldAxisUnits()[_axes[0]];
        original_params[0] = _major.getValue() * casacore::Quantity(abs(inc), unit);
    }

    if (_minor.getUnit().startsWith("pix")) {
        auto inc = csys.increment()[_axes[1]];
        auto unit = csys.worldAxisUnits()[_axes[1]];
        original_params[1] = _minor.getValue() * casacore::Quantity(abs(inc), unit);
    }

    auto kernel_params = original_params;

    // Figure out output image restoring beam (if any), output units and scale factor for convolution kernel array
    casacore::GaussianBeam beam_out;
    const auto& image_info = image_in.imageInfo();
    const auto& brightness_unit = image_in.units();

    casacore::String brightness_unit_out;
    auto image_info_out = image_out->imageInfo();
    auto log_factors = false;
    casacore::Double factor1 = -1;
    double pixel_area = 0;

    auto brightness_unit_up = brightness_unit.getName();
    brightness_unit_up.upcase();
    log_factors = brightness_unit_up.contains("/BEAM");
    if (log_factors) {
        pixel_area = csys.directionCoordinate().getPixelArea().getValue("arcsec*arcsec");
    }

    if (image_info.hasMultipleBeams()) {
        DoMultipleBeams(image_info_out, kernel_volume, image_out, brightness_unit_out, beam_out, factor1, image_in, original_params,
            kernel_params, kernel, log_factors, pixel_area);
    } else {
        DoSingleBeam(image_info_out, kernel_volume, kernel_params, kernel, brightness_unit_out, beam_out, image_out, image_in,
            original_params, log_factors, factor1, pixel_area);
    }

    image_out->setUnits(brightness_unit_out);
    image_out->setImageInfo(image_info_out);

    LogBeamInfo(image_info, "Original " + _image->name());
    LogBeamInfo(image_info_out, "Output ");
}

template <class T>
void Image2DConvolver<T>::DoSingleBeam(casacore::ImageInfo& image_info_out, casacore::Double& kernel_volume,
    vector<casacore::Quantity>& kernel_params, casacore::Array<casacore::Double>& kernel, casacore::String& brightness_unit_out,
    casacore::GaussianBeam& beam_out, SPIIT image_out, const casacore::ImageInterface<T>& image_in,
    const vector<casacore::Quantity>& original_params, casacore::Bool log_factors, casacore::Double factor1,
    casacore::Double pixel_area) const {
    GaussianBeam input_beam = image_in.imageInfo().restoringBeam();

    kernel_params = GetConvolvingBeamForTargetResolution(original_params, input_beam);
    spdlog::debug("Convolving image that has a beam of {} with a Gaussian of {} to reach a target resolution of {}",
        GetGaussianInfo(input_beam), GetGaussianInfo(GaussianBeam(kernel_params)), GetGaussianInfo(GaussianBeam(original_params)));

    kernel_volume = MakeKernel(kernel, kernel_params, image_in);

    const CoordinateSystem& csys = image_in.coordinates();
    auto scale_factor =
        DealWithRestoringBeam(brightness_unit_out, beam_out, kernel, kernel_volume, kernel_params, csys, input_beam, image_in.units());

    string message = "Scaling pixel values by ";

    if (log_factors) {
        casacore::GaussianBeam kernelBeam(kernel_params);
        factor1 = pixel_area / kernelBeam.getArea("arcsec*arcsec");
        casacore::Double factor2 = beam_out.getArea("arcsec*arcsec") / input_beam.getArea("arcsec*arcsec");
        message += fmt::format(
            "inverse of area of convolution kernel in pixels ({:.6f}) times the ratio of the beam areas ({:.6f}) = ", factor1, factor2);
    }

    message += fmt::format("{:.6f}", scale_factor);
    spdlog::debug(message);

    if (casacore::near(beam_out.getMajor(), beam_out.getMinor(), 1e-7)) {
        // circular beam should have same PA as given by user if targetres
        beam_out.setPA(original_params[2]);
    }

    // Convolve. We have already scaled the convolution kernel (with some trickery cleverer than what ImageConvolver can do) so no more
    // scaling
    casa::ImageConvolver<T> casa_image_convolver;
    casacore::Array<T> mod_kernel(kernel.shape());
    casacore::convertArray(mod_kernel, scale_factor * kernel);
    casa_image_convolver.convolve(*_log, *image_out, image_in, mod_kernel, casa::ImageConvolver<T>::NONE, 1.0, true);

    // Overwrite some bits and pieces in the output image to do with the restoring beam  and image units
    casacore::Bool holds_one_sky_axis;
    casacore::Bool has_sky = casacore::CoordinateUtil::holdsSky(holds_one_sky_axis, csys, _axes.asVector());

    if (has_sky && !beam_out.isNull()) {
        image_info_out.setRestoringBeam(beam_out);
    } else {
        // If one of the axes is in the sky plane, we must delete the restoring beam as it is no longer meaningful
        if (holds_one_sky_axis) {
            spdlog::warn("Because you convolved just one of the sky axes, the output image does not have a valid spatial restoring beam.");
            image_info_out.removeRestoringBeam();
        }
    }
}

template <class T>
void Image2DConvolver<T>::DoMultipleBeams(casacore::ImageInfo& image_info_out, casacore::Double& kernel_volume, SPIIT image_out,
    casacore::String& brightness_unit_out, casacore::GaussianBeam& beam_out, casacore::Double factor1,
    const casacore::ImageInterface<T>& image_in, const vector<Quantity>& original_params, vector<Quantity>& kernel_params,
    casacore::Array<casacore::Double>& kernel, casacore::Bool log_factors, casacore::Double pixel_area) const {
    casa::ImageMetaData<T> md(image_out);
    auto nchan = md.nChannels();
    auto npol = md.nStokes();

    // initialize all beams to be null
    image_info_out.setAllBeams(nchan, npol, casacore::GaussianBeam());
    const auto& csys = image_in.coordinates();
    auto spectral_axis = csys.spectralAxisNumber();
    auto polarization_axis = csys.polarizationAxisNumber();

    casacore::IPosition start(image_in.ndim(), 0);
    auto end = image_in.shape();

    if (nchan > 0) {
        end[spectral_axis] = 1;
    }
    if (npol > 0) {
        end[polarization_axis] = 1;
    }

    casacore::Int channel = -1;
    casacore::Int polarization = -1;

    image_info_out.removeRestoringBeam();
    image_info_out.setRestoringBeam(casacore::GaussianBeam(kernel_params));

    casacore::uInt count = (nchan > 0 && npol > 0) ? nchan * npol : nchan > 0 ? nchan : npol;

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

        if (nchan > 0) {
            channel = i % nchan;
            start[spectral_axis] = channel;
        }

        if (npol > 0) {
            polarization = nchan > 1 ? (i - channel) % nchan : i;
            start[polarization_axis] = polarization;
        }

        casacore::Slicer slice(start, end);
        casacore::SubImage<T> sub_image(image_in, slice);
        casacore::CoordinateSystem sub_csys = sub_image.coordinates();

        if (sub_csys.hasSpectralAxis()) {
            auto sub_ref_pix = sub_csys.referencePixel();
            sub_ref_pix[spectral_axis] = 0;
            sub_csys.setReferencePixel(sub_ref_pix);
        }

        auto input_beam = image_in.imageInfo().restoringBeam(channel, polarization);
        bool do_convolve(true);

        string message;
        if (channel >= 0) {
            message += fmt::format("Channel {} of {}", channel, nchan);
            if (polarization >= 0) {
                message += ", ";
            }
        }
        if (polarization >= 0) {
            message += fmt::format("Polarization {} of {}", polarization, npol);
        }

        message += " ";

        if (casacore::near(input_beam, GaussianBeam(original_params), 1e-5, casacore::Quantity(1e-2, "arcsec"))) {
            do_convolve = false;
            message += fmt::format("Input beam is already near target resolution so this plane will not be convolved.");
        } else {
            kernel_params = GetConvolvingBeamForTargetResolution(original_params, input_beam);
            kernel_volume = MakeKernel(kernel, kernel_params, image_in);
            message += fmt::format(": Convolving image which has a beam of {} with a Gaussian of {} to reach a target resolution of {}",
                GetGaussianInfo(input_beam), GetGaussianInfo(GaussianBeam(kernel_params)), GetGaussianInfo(GaussianBeam(original_params)));
        }

        spdlog::debug(message);

        casacore::TempImage<T> sub_image_out(sub_image.shape(), sub_image.coordinates());
        if (do_convolve) {
            auto scale_factor = DealWithRestoringBeam(
                brightness_unit_out, beam_out, kernel, kernel_volume, kernel_params, sub_csys, input_beam, image_in.units());
            {
                string message("Scaling pixel values by ");
                if (log_factors) {
                    casacore::GaussianBeam kernelBeam(kernel_params);
                    factor1 = pixel_area / kernelBeam.getArea("arcsec*arcsec");
                    auto factor2 = beam_out.getArea("arcsec*arcsec") / input_beam.getArea("arcsec*arcsec");
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

            if (casacore::near(beam_out.getMajor(), beam_out.getMinor(), 1e-7)) {
                // circular beam should have same PA as given by user if targetres
                beam_out.setPA(original_params[2]);
            }

            casacore::Array<T> mod_kernel(kernel.shape());
            casacore::convertArray(mod_kernel, scale_factor * kernel);
            casa::ImageConvolver<T> aic;
            aic.convolve(*_log, sub_image_out, sub_image, mod_kernel, casa::ImageConvolver<T>::NONE, 1.0, true);
        } else {
            brightness_unit_out = image_in.units().getName();
            beam_out = input_beam;
            sub_image_out.put(sub_image.get());
        }

        auto do_mask = image_out->isMasked() && image_out->hasPixelMask();
        casacore::Lattice<casacore::Bool>* mask_out = 0;
        if (do_mask) {
            mask_out = &image_out->pixelMask();
            if (!mask_out->isWritable()) {
                do_mask = false;
            }
        }

        auto cursor_shape = sub_image_out.niceCursorShape();
        auto out_pos = start;
        casacore::LatticeStepper stepper(sub_image_out.shape(), cursor_shape, casacore::LatticeStepper::RESIZE);
        casacore::RO_MaskedLatticeIterator<T> iter(sub_image_out, stepper);
        for (iter.reset(); !iter.atEnd(); iter++) {
            casacore::IPosition tmp_cursor_shape = iter.cursorShape();
            image_out->putSlice(iter.cursor(), out_pos);
            if (do_mask) {
                mask_out->putSlice(iter.getMask(), out_pos);
            }
            out_pos = out_pos + tmp_cursor_shape;
        }
    }
}

template <class T>
Double Image2DConvolver<T>::MakeKernel(casacore::Array<casacore::Double>& kernel_array, const std::vector<casacore::Quantity>& parameters,
    const casacore::ImageInterface<T>& image_in) const {
    // Check number of parameters
    ThrowIf(parameters.size() != 3, "Gaussian kernels require exactly 3 parameters");

    // Convert kernel widths to pixels from world.  Demands major and minor both in pixels or both in world, else exception
    casacore::Vector<casacore::Double> pixel_parameters;
    const casacore::CoordinateSystem csys = image_in.coordinates();

    // Use the reference value for the shape conversion direction
    casacore::Vector<casacore::Quantity> world_parameters(5);
    for (casacore::uInt i = 0; i < 3; i++) {
        world_parameters(i + 2) = parameters[i];
    }

    const casacore::Vector<casacore::Double> ref_val = csys.referenceValue();
    const casacore::Vector<casacore::String> units = csys.worldAxisUnits();
    casacore::Int world_axis = csys.pixelAxisToWorldAxis(_axes(0));
    world_parameters(0) = casacore::Quantity(ref_val(world_axis), units(world_axis));
    world_axis = csys.pixelAxisToWorldAxis(_axes(1));
    world_parameters(1) = casacore::Quantity(ref_val(world_axis), units(world_axis));
    casa::SkyComponentFactory::worldWidthsToPixel(pixel_parameters, world_parameters, csys, _axes, false);

    // Create n-Dim kernel array shape
    auto kernel_shape = ShapeOfKernel(pixel_parameters, image_in.ndim());

    // Create kernel array. We will fill the n-Dim array (shape non-unity only for pixelAxes) through its 2D casacore::Matrix incarnation.
    // Aren't we clever.
    kernel_array = 0;
    kernel_array.resize(kernel_shape);
    auto kernel_array2 = kernel_array.nonDegenerate(_axes);
    auto kernel_matrix = static_cast<casacore::Matrix<Double>>(kernel_array2);

    // Fill kernel casacore::Matrix with functional (height unity)
    return FillKernel(kernel_matrix, kernel_shape, pixel_parameters);
}

template <class T>
Double Image2DConvolver<T>::DealWithRestoringBeam(casacore::String& brightness_unit_out, casacore::GaussianBeam& beam_out,
    const casacore::Array<Double>& kernel_array, casacore::Double kernel_volume, const casacore::Vector<casacore::Quantity>& parameters,
    const casacore::CoordinateSystem& csys, const casacore::GaussianBeam& beam_in, const casacore::Unit& brightness_unit_in) const {
    // Find out if convolution axes hold the sky. Scaling from Jy/beam and Jy/pixel only really makes sense if this is true
    casacore::Bool holdsOneSkyAxis;
    auto has_sky = casacore::CoordinateUtil::holdsSky(holdsOneSkyAxis, csys, _axes.asVector());
    if (has_sky) {
        const casacore::DirectionCoordinate dc = csys.directionCoordinate();
        auto inc = dc.increment();
        auto unit = dc.worldAxisUnits();
        casacore::Quantity x(inc[0], unit[0]);
        casacore::Quantity y(inc[1], unit[1]);
        auto diag = sqrt(x * x + y * y);
        auto min_axis = parameters[1];
        if (min_axis.getUnit().startsWith("pix")) {
            min_axis.setValue(min_axis.getValue() * x.getValue());
            min_axis.setUnit(x.getUnit());
        }
        if (min_axis < diag) {
            diag.convert(min_axis.getFullUnit());
            spdlog::debug(
                "Convolving kernel has minor axis {} which is less than the pixel diagonal length of {}. Thus, the kernel is "
                "poorly sampled, and so the output of this application may not be what you expect. You should consider increasing the "
                "kernel size or regridding the image to a smaller pixel size",
                GetQuantityInfo(min_axis), GetQuantityInfo(diag));
        } else if (beam_in.getMinor() < diag && beam_in != casacore::GaussianBeam::NULL_BEAM) {
            diag.convert(beam_in.getMinor().getFullUnit());
            spdlog::debug(
                "Input beam has minor axis {} which is less than the pixel diagonal length of {}. Thus, the beam is poorly "
                "sampled, and so the output of this application may not be what you expect. You should consider regridding the image "
                "to a smaller pixel size.",
                GetQuantityInfo(beam_in.getMinor()), GetQuantityInfo(diag));
        }
    }

    string tmp = (has_sky ? "" : "not");
    spdlog::debug("You are {} convolving the sky", tmp);

    beam_out = casacore::GaussianBeam();
    auto brightness_unit_in_upcase = casacore::upcase(brightness_unit_in.getName());
    const auto& ref_pix = csys.referencePixel();
    casacore::Double scale_factor = 1;
    brightness_unit_out = brightness_unit_in.getName();

    if (has_sky && brightness_unit_in_upcase.contains("/PIXEL")) {
        // Easy case. Peak of convolution kernel must be unity and output units are Jy/beam. All other cases require numerical convolution
        // of beams
        brightness_unit_out = "Jy/beam";

        // Exception already generated if only one of major and minor in pixel units
        auto maj_axis = parameters(0);
        auto min_axis = parameters(1);

        if (maj_axis.getFullUnit().getName() == "pix") {
            casacore::Vector<casacore::Double> pixelParameters(5);
            pixelParameters(0) = ref_pix(_axes(0));
            pixelParameters(1) = ref_pix(_axes(1));
            pixelParameters(2) = parameters(0).getValue();
            pixelParameters(3) = parameters(1).getValue();
            pixelParameters(4) = parameters(2).getValue(casacore::Unit("rad"));
            casacore::GaussianBeam world_parameters;
            casa::SkyComponentFactory::pixelWidthsToWorld(world_parameters, pixelParameters, csys, _axes, false);
            maj_axis = world_parameters.getMajor();
            min_axis = world_parameters.getMinor();
        }

        beam_out = casacore::GaussianBeam(maj_axis, min_axis, parameters(2));
    } else {
        // Is there an input restoring beam and are we convolving the sky to which it pertains?  If not, all we can do is use user scaling
        // or normalize the convolution kernel to unit volume.  There is no point to convolving the input beam either as it pertains only to
        // the sky
        if (has_sky && !beam_in.isNull()) {
            // Convert restoring beam parameters to pixels. Output pa is pos +x -> +y in pixel frame.
            casacore::Vector<casacore::Quantity> w_parameters(5);
            const auto ref_val = csys.referenceValue();
            const auto units = csys.worldAxisUnits();
            auto w_axis = csys.pixelAxisToWorldAxis(_axes(0));
            w_parameters(0) = casacore::Quantity(ref_val(w_axis), units(w_axis));
            w_axis = csys.pixelAxisToWorldAxis(_axes(1));
            w_parameters(1) = casacore::Quantity(ref_val(w_axis), units(w_axis));
            w_parameters(2) = beam_in.getMajor();
            w_parameters(3) = beam_in.getMinor();
            w_parameters(4) = beam_in.getPA(true);
            casacore::Vector<casacore::Double> d_parameters;
            casa::SkyComponentFactory::worldWidthsToPixel(d_parameters, w_parameters, csys, _axes, false);

            // Create 2-D beam array shape
            // casacore::IPosition dummyAxes(2, 0, 1);
            auto beam_shape = ShapeOfKernel(d_parameters, 2);

            // Create beam casacore::Matrix and fill with height unity
            casacore::Matrix<casacore::Double> beam_matrix_in(beam_shape(0), beam_shape(1));
            FillKernel(beam_matrix_in, beam_shape, d_parameters);
            auto shape = beam_matrix_in.shape();

            // Get 2-D version of convolution kenrel
            auto kernelArray2 = kernel_array.nonDegenerate(_axes);
            auto kernel_matrix = static_cast<casacore::Matrix<Double>>(kernelArray2);

            // Convolve input restoring beam array by convolution kernel array
            casacore::Matrix<Double> beam_matrix_out;
            casacore::Convolver<Double> conv(beam_matrix_in, kernel_matrix.shape());
            conv.linearConv(beam_matrix_out, kernel_matrix);

            // Scale kernel
            auto max_val_out = max(beam_matrix_out);
            scale_factor = 1 / max_val_out;

            casacore::Fit2D fitter(*_log);
            const casacore::uInt n = beam_matrix_out.shape()(0);
            auto beam_parameters = fitter.estimate(casacore::Fit2D::GAUSSIAN, beam_matrix_out);
            casacore::Vector<casacore::Bool> beam_parameter_mask(beam_parameters.nelements(), true);
            beam_parameters(1) = (n - 1) / 2;        // x centre
            beam_parameters(2) = beam_parameters(1); // y centre

            // Set range so we don't include too many pixels in fit which will make it very slow
            fitter.addModel(casacore::Fit2D::GAUSSIAN, beam_parameters, beam_parameter_mask);
            casacore::Array<Double> sigma;
            fitter.setIncludeRange(max_val_out / 10.0, max_val_out + 0.1);
            auto error = fitter.fit(beam_matrix_out, sigma);
            ThrowIf(error == casacore::Fit2D::NOCONVERGE || error == casacore::Fit2D::FAILED || error == casacore::Fit2D::NOGOOD,
                "Failed to fit the output beam");

            auto beam_solution = fitter.availableSolution();
            // Convert to world units.
            casacore::Vector<casacore::Double> pixel_parameters(5);
            pixel_parameters(0) = ref_pix(_axes(0));
            pixel_parameters(1) = ref_pix(_axes(1));
            pixel_parameters(2) = beam_solution(3);
            pixel_parameters(3) = beam_solution(4);
            pixel_parameters(4) = beam_solution(5);
            casa::SkyComponentFactory::pixelWidthsToWorld(beam_out, pixel_parameters, csys, _axes, false);

            if (!brightness_unit_in.getName().contains(casacore::Regex(Regex::makeCaseInsensitive("beam")))) {
                scale_factor *= beam_in.getArea("arcsec2") / beam_out.getArea("arcsec2");
            }
        } else {
            // Conserve flux is the best we can do
            scale_factor = 1 / kernel_volume;
        }
    }

    // Put beam position angle into range +/- 180 in case it has eluded us so far
    if (!beam_out.isNull()) {
        casacore::MVAngle pa(beam_out.getPA(true).getValue(casacore::Unit("rad")));
        pa();
        beam_out = casacore::GaussianBeam(beam_out.getMajor(), beam_out.getMinor(), casacore::Quantity(pa.degree(), casacore::Unit("deg")));
    }

    return scale_factor;
}

template <class T>
casacore::IPosition Image2DConvolver<T>::ShapeOfKernel(
    const casacore::Vector<casacore::Double>& parameters, const casacore::uInt ndim) const {
    // Work out how big the array holding the kernel should be simplest algorithm possible. Shape is presently square.
    // Find 2D shape
    casacore::uInt n;
    casacore::uInt n1 = SizeOfGaussian(parameters(0), 5.0);
    casacore::uInt n2 = SizeOfGaussian(parameters(1), 5.0);
    n = max(n1, n2);
    if (n % 2 == 0) {
        n++; // Make shape odd so centres well
    }

    // Now find the shape for the image and slot the 2D shape in in the correct axis locations
    casacore::IPosition shape(ndim, 1);
    shape(_axes(0)) = n;
    shape(_axes(1)) = n;
    return shape;
}

template <class T>
uInt Image2DConvolver<T>::SizeOfGaussian(const casacore::Double width, const casacore::Double nsigma) const {
    // +/- 5 sigma is a volume error of less than 6e-5%
    casacore::Double sigma = width / sqrt(casacore::Double(8.0) * C::ln2);
    return (casacore::Int(nsigma * sigma + 0.5) + 1) * 2;
}

template <class T>
Double Image2DConvolver<T>::FillKernel(casacore::Matrix<casacore::Double>& kernel_matrix, const casacore::IPosition& kernel_shape,
    const casacore::Vector<casacore::Double>& parameters) const {
    // Centre functional in array (shape is odd)
    auto x_centre = casacore::Double((kernel_shape[_axes[0]] - 1) / 2.0);
    auto y_centre = casacore::Double((kernel_shape[_axes[1]] - 1) / 2.0);
    casacore::Double height = 1;

    // Create functional. We only have gaussian2d functions at this point.  Later the filling code can be moved out of the if statement
    Double max_val_kernel;
    Double volume_kernel = 0;
    auto pa = parameters[2];
    auto ratio = parameters[1] / parameters[0];
    auto major = parameters[0];

    FillGaussian(max_val_kernel, volume_kernel, kernel_matrix, height, x_centre, y_centre, major, ratio, pa);

    return volume_kernel;
}

template <class T>
void Image2DConvolver<T>::FillGaussian(casacore::Double& max_val, casacore::Double& volume, casacore::Matrix<casacore::Double>& pixels,
    casacore::Double height, casacore::Double x_centre, casacore::Double y_centre, casacore::Double major_axis, casacore::Double ratio,
    casacore::Double position_angle) const {
    // pa is positive in +x -> +y pixel coordinate frame
    casacore::uInt n1 = pixels.shape()(0);
    casacore::uInt n2 = pixels.shape()(1);
    AlwaysAssert(n1 == n2, casacore::AipsError);

    position_angle += C::pi_2; // +y -> -x
    casacore::Gaussian2D<Double> gaussian_2d(height, x_centre, y_centre, major_axis, ratio, position_angle);

    max_val = -1.0e30;
    volume = 0.0;
    casacore::Vector<Double> pos(2);

    for (casacore::uInt j = 0; j < n1; ++j) {
        pos[1] = j;
        for (casacore::uInt i = 0; i < n1; ++i) {
            pos[0] = i;
            casacore::Double val = gaussian_2d(pos);
            pixels(i, j) = val;
            max_val = max(val, max_val);
            volume += val;
        }
    }
}

template <class T>
std::vector<casacore::Quantity> Image2DConvolver<T>::GetConvolvingBeamForTargetResolution(
    const std::vector<casacore::Quantity>& target_beam_params, const casacore::GaussianBeam& input_beam) const {
    casacore::GaussianBeam convolving_beam;
    casacore::GaussianBeam target_beam(target_beam_params[0], target_beam_params[1], target_beam_params[2]);

    try {
        if (casa::GaussianDeconvolver::deconvolve(convolving_beam, target_beam, input_beam)) {
            // point source, or convolving_beam nonsensical
            throw casacore::AipsError();
        }
    } catch (const casacore::AipsError& x) {
        std::string error = fmt::format(
            "Unable to reach target resolution of {}. Input image beam {} is (nearly) identical to or larger than the output beam size.",
            GetGaussianInfo(target_beam), GetGaussianInfo(input_beam));
        spdlog::error(error);
        ThrowCc(error);
    }

    std::vector<casacore::Quantity> kernel_params{convolving_beam.getMajor(), convolving_beam.getMinor(), convolving_beam.getPA(true)};

    return kernel_params;
}

template <class T>
SPIIT Image2DConvolver<T>::PrepareOutputImage(const casacore::ImageInterface<T>& image, casacore::Bool drop_deg) const {
    casacore::String out_name;
    casacore::Bool overwrite(false);
    static const casacore::Record empty;
    static const casacore::String empty_string;
    auto out_image = casa::SubImageFactory<T>::createImage(image, out_name, empty, empty_string, drop_deg, overwrite, true, false, false);
    return out_image;
}

template <class T>
void Image2DConvolver<T>::LogBeamInfo(const casacore::ImageInfo& imageInfo, const casacore::String& desc) const {
    const auto& beam_set = imageInfo.getBeamSet();
    string message = desc;

    if (!imageInfo.hasBeam()) {
        message += " has no beam";
    } else if (imageInfo.hasSingleBeam()) {
        message += fmt::format(" resolution {} ", GetGaussianInfo(beam_set.getBeam()));
    } else {
        message += fmt::format(" has multiple beams. Min area beam: {}. Max area beam: {}. Median area beam {}",
            GetGaussianInfo(beam_set.getMinAreaBeam()), GetGaussianInfo(beam_set.getMaxAreaBeam()),
            GetGaussianInfo(beam_set.getMedianAreaBeam()));
    }
    spdlog::debug(message);
}

template <class T>
void Image2DConvolver<T>::CopyMask(casacore::Lattice<casacore::Bool>& mask, const casacore::ImageInterface<T>& image) {
    auto cursor_shape = image.niceCursorShape(4096 * 4096);
    casacore::LatticeStepper stepper(image.shape(), cursor_shape, casacore::LatticeStepper::RESIZE);
    casacore::RO_MaskedLatticeIterator<T> image_iter(image, stepper);
    casacore::LatticeIterator<casacore::Bool> mask_iter(mask, stepper);
    std::unique_ptr<casacore::RO_LatticeIterator<casacore::Bool>> image_pixel_mask_iter;
    if (image.hasPixelMask()) {
        image_pixel_mask_iter.reset(new casacore::RO_LatticeIterator<casacore::Bool>(image.pixelMask(), stepper));
    }

    for (image_iter.reset(); !image_iter.atEnd(); ++image_iter, ++mask_iter) {
        auto my_mask = image_iter.getMask();
        if (image_pixel_mask_iter) {
            my_mask = my_mask && image_pixel_mask_iter->cursor();
            image_pixel_mask_iter->operator++();
        }
        mask_iter.rwCursor() = my_mask;
    }
}

template <class T>
void Image2DConvolver<T>::StopCalculation() {
    _stop = true;
}

#endif // CARTA_BACKEND__MOMENT_IMAGE2DCONVOLVER_H_