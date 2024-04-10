/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Deconvolver.h"

#include <casacore/casa/Quanta/MVAngle.h>

namespace carta {

Deconvolver::Deconvolver(casacore::CoordinateSystem coord_sys, casacore::GaussianBeam beam, double residue_rms)
    : _coord_sys(coord_sys), _beam(beam), _residue_rms(residue_rms) {
    _noise_FWHM = casacore::Quantity(casacore::sqrt(_beam.getMajor() * _beam.getMinor()).get("arcsec"));
}

void Deconvolver::GetDeconvolutionResults(
    const std::vector<CARTA::GaussianComponent>& in_gauss_vec, std::string& log, std::vector<DeconvolutionResult>& pixel_results) {
    log += "\n------------- Deconvolved from beam -------------\n";
    for (int i = 0; i < in_gauss_vec.size(); ++i) {
        const CARTA::GaussianComponent& in_gauss = in_gauss_vec[i];
        DeconvolutionResult world_result;
        if (DoDeconvolution(in_gauss, world_result)) {
            std::string major = fmt::format("{:.6f}", world_result.major.getValue());
            std::string minor = fmt::format("{:.6f}", world_result.minor.getValue());
            std::string pa = fmt::format("{:.6f}", world_result.pa.getValue());

            std::string err_major = fmt::format("{:.6f}", world_result.major_err.getValue());
            std::string err_minor = fmt::format("{:.6f}", world_result.minor_err.getValue());
            std::string err_pa = fmt::format("{:.6f}", world_result.pa_err.getValue());

            std::string unit_major = world_result.major.getUnit();
            std::string unit_minor = world_result.minor.getUnit();
            std::string unit_pa = world_result.pa.getUnit();

            DeconvolutionResult pixel_result;
            bool pixel_params_available = GetWorldWidthToPixel(world_result, pixel_result);

            log += fmt::format("Component #{}:\n", i + 1);
            log += fmt::format("FWHM Major Axis = {} +/- {} ({})\n", major, err_major, unit_major);
            if (pixel_params_available) {
                auto major_pixel = pixel_result.major.getValue();
                auto major_err_pixel = pixel_result.major_err.getValue();
                log += fmt::format("                = {:.6f} +/- {:.6f} (px)\n", major_pixel, major_err_pixel);
            }
            log += fmt::format("FWHM Minor Axis = {} +/- {} ({})\n", minor, err_minor, unit_minor);
            if (pixel_params_available) {
                auto minor_pixel = pixel_result.minor.getValue();
                auto minor_err_pixel = pixel_result.minor_err.getValue();
                log += fmt::format("                = {:.6f} +/- {:.6f} (px)\n", minor_pixel, minor_err_pixel);
            }
            log += fmt::format("P.A.            = {} +/- {} ({})\n", pa, err_pa, unit_pa);

            if (pixel_params_available) {
                pixel_results.emplace_back(pixel_result);
            }
        }
    }
    log += "---------------------- End ----------------------\n";
}

bool Deconvolver::DoDeconvolution(const CARTA::GaussianComponent& in_gauss, DeconvolutionResult& result) {
    casacore::Double center_x = in_gauss.center().x();
    casacore::Double center_y = in_gauss.center().y();
    casacore::Double fwhm_x = in_gauss.fwhm().x();
    casacore::Double fwhm_y = in_gauss.fwhm().y();
    casacore::Double pa = in_gauss.pa(); // in the unit of *degree*

    auto ori_gauss_shape = PixelToWorld(center_x, center_y, fwhm_x, fwhm_y, pa);
    casacore::Quantity ori_major = ori_gauss_shape.fwhm_major;
    casacore::Quantity ori_minor = ori_gauss_shape.fwhm_minor;
    casacore::Quantity ori_pa = ori_gauss_shape.pa;

    // Get deconvolved gaussian
    bool success(false);
    casacore::GaussianBeam best_sol(ori_major, ori_minor, ori_pa);
    casacore::GaussianBeam best_decon_sol;
    casacore::Bool is_point_source(true);
    try {
        is_point_source = Deconvolve(best_decon_sol, best_sol, _beam);
        success = true;
    } catch (const casacore::AipsError& x) {
        is_point_source = true;
    }

    // Calculate errors for deconvolved gaussian from original fit results
    double peak_intensities = in_gauss.amp();
    double base_fac = casacore::C::sqrt2 / CorrelatedOverallSNR(peak_intensities, ori_major, ori_minor, 0.5, 2.5);
    double ori_major_val = ori_major.getValue("arcsec");
    double ori_minor_val = ori_minor.getValue("arcsec");
    casacore::Quantity err_pa =
        ori_major_val == ori_minor_val
            ? casacore::QC::qTurn()
            : casacore::Quantity(base_fac * casacore::C::sqrt2 *
                                     (ori_major_val * ori_minor_val / (ori_major_val * ori_major_val - ori_minor_val * ori_minor_val)),
                  "rad");
    err_pa.convert(ori_pa);

    // Set deconvolved results
    casacore::Quantity err_major = casacore::C::sqrt2 / CorrelatedOverallSNR(peak_intensities, ori_major, ori_minor, 2.5, 0.5) * ori_major;
    casacore::Quantity err_minor = casacore::C::sqrt2 / CorrelatedOverallSNR(peak_intensities, ori_major, ori_minor, 0.5, 2.5) * ori_minor;

    if (success) {
        if (!is_point_source) {
            casacore::Vector<casacore::Quantity> major_range(2, ori_major - err_major);
            major_range[1] = ori_major + err_major;
            casacore::Vector<casacore::Quantity> minor_range(2, ori_minor - err_minor);
            minor_range[1] = ori_minor + err_minor;
            casacore::Vector<casacore::Quantity> pa_range(2, ori_pa - err_pa);
            pa_range[1] = ori_pa + err_pa;
            casacore::GaussianBeam source_in;
            casacore::Quantity my_major;
            casacore::Quantity my_minor;
            casacore::GaussianBeam decon_beam;

            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    my_major = casacore::max(major_range[i], minor_range[j]);
                    my_minor = casacore::min(major_range[i], minor_range[j]);
                    if (my_major.getValue() > 0 && my_minor.getValue() > 0) {
                        source_in.setMajorMinor(my_major, my_minor);
                        for (int k = 0; k < 2; k++) {
                            source_in.setPA(pa_range[k]);
                            decon_beam = casacore::GaussianBeam();
                            casacore::Bool is_point;

                            try {
                                is_point = Deconvolve(decon_beam, source_in, _beam);
                            } catch (const casacore::AipsError& x) {
                                is_point = true;
                            }

                            if (!is_point) {
                                casacore::Quantity tmp_err_major = abs(best_decon_sol.getMajor() - decon_beam.getMajor());
                                tmp_err_major.convert(err_major.getUnit());

                                casacore::Quantity tmp_err_minor = abs(best_decon_sol.getMinor() - decon_beam.getMinor());
                                tmp_err_minor.convert(err_minor.getUnit());

                                casacore::Quantity tmp_err_pa = abs(best_decon_sol.getPA(true) - decon_beam.getPA(true));
                                tmp_err_pa = casacore::min(tmp_err_pa, abs(tmp_err_pa - casacore::QC::hTurn()));
                                tmp_err_pa.convert(err_pa.getUnit());

                                err_major = casacore::max(err_major, tmp_err_major);
                                err_minor = casacore::max(err_minor, tmp_err_minor);
                                err_pa = casacore::max(err_pa, tmp_err_pa);
                            }
                        }
                    }
                }
            }
            // Get center x and y in world coordinates
            auto coord_dir = _coord_sys.directionCoordinate();
            casacore::Vector<casacore::Double> center_world(2, 0);
            coord_dir.toWorld(center_world, {center_x, center_y});
            const casacore::Vector<casacore::String> world_units = coord_dir.worldAxisUnits();

            result = DeconvolutionResult(in_gauss.amp(), casacore::Quantity(center_world(0), world_units(0)),
                casacore::Quantity(center_world(1), world_units(1)), best_decon_sol.getMajor(), best_decon_sol.getMinor(),
                best_decon_sol.getPA(true), err_major, err_minor, err_pa);
        }
    }
    return success;
}

double Deconvolver::CorrelatedOverallSNR(double peak_intensities, casacore::Quantity major, casacore::Quantity minor, double a, double b) {
    double signal_to_noise = std::abs(peak_intensities) / _residue_rms;
    double fac = signal_to_noise / 2 * (casacore::sqrt(major * minor) / (_noise_FWHM)).getValue("");
    double p = (_noise_FWHM / major).getValue("");
    double fac1 = std::pow(1 + p * p, a / 2);
    double q = (_noise_FWHM / minor).getValue("");
    double fac2 = std::pow(1 + q * q, b / 2);
    return fac * fac1 * fac2;
}

bool Deconvolver::GetWorldWidthToPixel(const DeconvolutionResult& world_coords, DeconvolutionResult& pixel_coords) {
    casacore::Vector<casacore::Double> pixels(3, 0);
    casacore::Vector<casacore::Double> pixels_err1(3, 0);
    casacore::Vector<casacore::Double> pixels_err2(3, 0);
    casacore::Quantity major = world_coords.major;
    casacore::Quantity minor = world_coords.minor;
    casacore::Quantity pa = world_coords.pa;
    casacore::Quantity major_err = world_coords.major_err;
    casacore::Quantity minor_err = world_coords.minor_err;
    casacore::Quantity pa_err = world_coords.pa_err;

    casacore::Vector<casacore::Quantity> world_params = {world_coords.center_x, world_coords.center_y, major, minor, pa};
    bool pixels_available = WorldWidthToPixel(pixels, world_params);

    casacore::Vector<casacore::Quantity> world_params_err1 = {
        world_coords.center_x, world_coords.center_y, major + major_err, minor + minor_err, pa};
    bool pixels_err1_available = WorldWidthToPixel(pixels_err1, world_params_err1);

    casacore::Vector<casacore::Quantity> world_params_err2 = {
        world_coords.center_x, world_coords.center_y, major - major_err, minor - minor_err, pa};
    bool pixels_err2_available = WorldWidthToPixel(pixels_err2, world_params_err2);

    casacore::Vector<casacore::Double> pixels_err(3, 0);
    for (int i = 0; i < pixels_err.size(); ++i) {
        pixels_err(i) = std::abs(pixels_err1(i) - pixels_err2(i)) / 2;
    }

    auto coord_dir = _coord_sys.directionCoordinate();
    casacore::Vector<casacore::Double> center_pixel(2, 0);
    casacore::Vector<casacore::Double> center_world = {world_coords.center_x.getValue(), world_coords.center_y.getValue()};
    coord_dir.toPixel(center_pixel, center_world);

    pixel_coords = DeconvolutionResult(world_coords.amplitude, center_pixel(0), center_pixel(1), pixels(0), pixels(1), pixels(2),
        pixels_err(0), pixels_err(1), pixels_err(2));

    return pixels_available && pixels_err1_available && pixels_err2_available;
}

bool Deconvolver::WorldWidthToPixel(
    casacore::Vector<casacore::Double>& pixel_params, const casacore::Vector<casacore::Quantity>& world_params) {
    casacore::IPosition pixel_axes = {0, 1};
    try {
        CalcWorldWidthToPixel(pixel_params, world_params, pixel_axes);
    } catch (const casacore::AipsError& x) {
        spdlog::error("Fail to convert 2D Gaussian world width to pixel: {}", x.getMesg());
        return false;
    }
    return true;
}

GaussianShape Deconvolver::PixelToWorld(
    casacore::Double center_x, casacore::Double center_y, casacore::Double fwhm_x, casacore::Double fwhm_y, casacore::Double pa) {
    casacore::MDirection mean_dir;
    casacore::Vector<casacore::Double> center = {center_x, center_y};
    casacore::DirectionCoordinate dir_coord = _coord_sys.directionCoordinate();
    dir_coord.toWorld(mean_dir, center);

    pa = (pa + 90) * casacore::C::pi / 180 - casacore::C::pi_2; // rotate 90 degrees and convert to radians
    casacore::MDirection tip_major = DirectionFromCartesian(center_x, center_y, fwhm_x, pa, dir_coord);
    pa += casacore::C::pi_2;
    casacore::MDirection tip_minor = DirectionFromCartesian(center_x, center_y, fwhm_y, pa, dir_coord);

    casacore::MVDirection mvd_ref = mean_dir.getValue();
    casacore::MVDirection mvd_major = tip_major.getValue();
    casacore::MVDirection mvd_minor = tip_minor.getValue();

    casacore::Double tmp_fwhm_major = 2 * mvd_ref.separation(mvd_major) * 3600 * 180.0 / casacore::C::pi;
    casacore::Double tmp_fwhm_minor = 2 * mvd_ref.separation(mvd_minor) * 3600 * 180.0 / casacore::C::pi;

    casacore::Quantity result_fwhm_major(casacore::max(tmp_fwhm_major, tmp_fwhm_minor), casacore::Unit("arcsec"));
    casacore::Quantity result_fwhm_minor(casacore::min(tmp_fwhm_major, tmp_fwhm_minor), casacore::Unit("arcsec"));
    casacore::Bool flipped(tmp_fwhm_minor > tmp_fwhm_major);
    casacore::Quantity result_pa;
    if (!flipped) {
        result_pa = mvd_ref.positionAngle(mvd_major, casacore::Unit("deg"));
    } else {
        result_pa = mvd_ref.positionAngle(mvd_minor, casacore::Unit("deg"));
    }

    return {result_fwhm_major, result_fwhm_minor, result_pa};
}

casacore::MDirection Deconvolver::DirectionFromCartesian(casacore::Double center_x, casacore::Double center_y, casacore::Double width,
    casacore::Double pa, const casacore::DirectionCoordinate& dir_coord) {
    casacore::Double z = width / 2.0;
    casacore::Double x = -z * sin(pa);
    casacore::Double y = z * cos(pa);
    casacore::MDirection mdir;
    casacore::Vector<casacore::Double> pixel_tip(2);
    pixel_tip(0) = center_x + x;
    pixel_tip(1) = center_y + y;
    dir_coord.toWorld(mdir, pixel_tip);
    return mdir;
}

bool Deconvolver::Deconvolve(
    casacore::GaussianBeam& deconvolved_size, const casacore::GaussianBeam& convolved_size, const casacore::GaussianBeam& beam) {
    casacore::Unit radians(casacore::String("rad"));
    casacore::Unit position_angle_model_unit = deconvolved_size.getPA(false).getFullUnit();
    casacore::Unit major_axis_model_unit = deconvolved_size.getMajor().getFullUnit();
    casacore::Unit minor_axis_model_unit = deconvolved_size.getMinor().getFullUnit();

    // Get values in radians
    casacore::Double major_source = convolved_size.getMajor().getValue(radians);
    casacore::Double minor_source = convolved_size.getMinor().getValue(radians);
    casacore::Double theta_source = convolved_size.getPA(true).getValue(radians);
    casacore::Double major_beam = beam.getMajor().getValue(radians);
    casacore::Double minor_beam = beam.getMinor().getValue(radians);
    casacore::Double theta_beam = beam.getPA(true).getValue(radians);

    // Do the sums
    casacore::Double alpha = casacore::square(major_source * cos(theta_source)) + casacore::square(minor_source * sin(theta_source)) -
                             casacore::square(major_beam * cos(theta_beam)) - casacore::square(minor_beam * sin(theta_beam));
    casacore::Double beta = casacore::square(major_source * sin(theta_source)) + casacore::square(minor_source * cos(theta_source)) -
                            casacore::square(major_beam * sin(theta_beam)) - casacore::square(minor_beam * cos(theta_beam));
    casacore::Double gamma =
        2 * ((casacore::square(minor_source) - casacore::square(major_source)) * sin(theta_source) * cos(theta_source) -
                (casacore::square(minor_beam) - casacore::square(major_beam)) * sin(theta_beam) * cos(theta_beam));

    // Set result in radians
    casacore::Double s = alpha + beta;
    casacore::Double t = sqrt(casacore::square(alpha - beta) + casacore::square(gamma));
    casacore::Double limit = casacore::min(major_source, minor_source);
    limit = casacore::min(limit, major_beam);
    limit = casacore::min(limit, minor_beam);
    limit = 0.1 * limit * limit;

    if (alpha < 0.0 || beta < 0.0 || s < t) {
        if (0.5 * (s - t) < limit && alpha > -limit && beta > -limit) {
            // Point source. Fill in values of beam
            deconvolved_size = casacore::GaussianBeam(casacore::Quantity(beam.getMajor().get(major_axis_model_unit)),
                casacore::Quantity(beam.getMinor().get(minor_axis_model_unit)),
                casacore::Quantity(beam.getPA(true).get(position_angle_model_unit)));
            deconvolved_size.setPA(deconvolved_size.getPA(true));
            return true;
        } else {
            throw casacore::AipsError("Source may be only (slightly) resolved in one direction");
        }
    }
    casacore::Quantity majax(sqrt(0.5 * (s + t)), radians);
    majax.convert(major_axis_model_unit);
    casacore::Quantity minax(sqrt(0.5 * (s - t)), radians);
    minax.convert(minor_axis_model_unit);
    casacore::Quantity pa(abs(gamma) + abs(alpha - beta) == 0.0 ? 0.0 : 0.5 * atan2(-gamma, alpha - beta), radians);
    pa.convert(position_angle_model_unit);
    deconvolved_size = casacore::GaussianBeam(majax, minax, pa);
    deconvolved_size.setPA(deconvolved_size.getPA(true));
    return false;
}

void Deconvolver::CalcWorldWidthToPixel(casacore::Vector<casacore::Double>& pixel_params,
    const casacore::Vector<casacore::Quantity>& world_params, const casacore::IPosition& dir_axes) {
    ThrowIf(dir_axes.nelements() != 2, "You must give two pixel axes");
    ThrowIf(world_params.nelements() != 5, "The world parameters vector must be of length 5.");

    pixel_params.resize(3);
    casacore::Int c0, c1, axis_in_coord0, axis_in_coord1;
    _coord_sys.findPixelAxis(c0, axis_in_coord0, dir_axes(0));
    _coord_sys.findPixelAxis(c1, axis_in_coord1, dir_axes(1));

    // Get units
    casacore::String major_unit = world_params(2).getFullUnit().getName();
    casacore::String minor_unit = world_params(3).getFullUnit().getName();

    // This saves me trying to handle mixed pixel/world units which is a pain for coupled coordinates
    ThrowIf((major_unit == casacore::String("pix") && minor_unit != casacore::String("pix")) ||
                (major_unit != casacore::String("pix") && minor_unit == casacore::String("pix")),
        "If pixel units are used, both major and minor axes must have pixel units");

    // Some checks
    casacore::Coordinate::Type type0 = _coord_sys.type(c0);
    casacore::Coordinate::Type type1 = _coord_sys.type(c1);
    ThrowIf(type0 != type1 && (major_unit != casacore::String("pix") || minor_unit != casacore::String("pix")),
        "The coordinate types for the convolution axes are different. "
        "Therefore the units of the major and minor axes of "
        "the convolution kernel widths must both be pixels.");
    ThrowIf(type0 == casacore::Coordinate::DIRECTION && type1 == casacore::Coordinate::DIRECTION && c0 != c1,
        "The given axes do not come from the same Direction coordinate. "
        "This situation requires further code development.");
    ThrowIf(type0 == casacore::Coordinate::STOKES || type1 == casacore::Coordinate::STOKES, "Cannot convolve Stokes axes.");

    // Deal with pixel units separately. Both are in pixels if either is in pixels. Continue on if non-pixel units
    if (type0 == casacore::Coordinate::DIRECTION && type1 == casacore::Coordinate::DIRECTION) {
        // Check units are angular
        casacore::Unit rad(casacore::String("rad"));
        ThrowIf(!world_params(2).check(rad.getValue()), "The units of the major axis must be angular");
        ThrowIf(!world_params(3).check(rad.getValue()), "The units of the minor axis must be angular");

        // Make a Gaussian shape to convert to pixels at specified location
        const casacore::DirectionCoordinate& dir_coord = _coord_sys.directionCoordinate(c0);
        casacore::MDirection world;
        if (!dir_coord.toWorld(world, dir_coord.referencePixel())) {
            world = casacore::MDirection(world_params(0), world_params(1), dir_coord.directionType());
        }

        casacore::Vector<casacore::Double> pars = ToPixel(world, world_params(2), world_params(3), world_params(4));
        pixel_params(0) = pars(2);
        pixel_params(1) = pars(3);
        pixel_params(2) = pars(4); // radians; +x -> +y
    } else {
        // Find major and minor axes in pixels
        pixel_params(0) = CalcAltWorldWidthToPixel(pixel_params(2), world_params(2), dir_axes);
        pixel_params(1) = CalcAltWorldWidthToPixel(pixel_params(2), world_params(3), dir_axes);
        pixel_params(2) = world_params(4).getValue(casacore::Unit("rad")); // radians; +x -> +y
    }

    // Make sure major > minor
    casacore::Double tmp = pixel_params(0);
    pixel_params(0) = casacore::max(tmp, pixel_params(1));
    pixel_params(1) = casacore::min(tmp, pixel_params(1));
}

casacore::Double Deconvolver::CalcAltWorldWidthToPixel(
    const casacore::Double& pa, const casacore::Quantity& length, const casacore::IPosition& pixel_axes) {
    casacore::Int worldAxis0 = _coord_sys.pixelAxisToWorldAxis(pixel_axes(0));
    casacore::Int worldAxis1 = _coord_sys.pixelAxisToWorldAxis(pixel_axes(1));

    // Units of the axes must be consistent
    casacore::Vector<casacore::String> units = _coord_sys.worldAxisUnits();
    casacore::Unit unit0(units(worldAxis0));
    casacore::Unit unit1(units(worldAxis1));
    ThrowIf(unit0 != unit1, "Units of the two axes must be conformant");
    casacore::Unit unit(unit0);

    // Check units
    if (!length.check(unit.getValue())) {
        auto error = fmt::format("The units of the world length ({}) are not consistent with those of coordinate system ({})",
            length.getFullUnit().getName(), unit.getName());
        ThrowCc(error);
    }

    // Find pixel coordinate of tip of axis  relative to reference pixel
    casacore::Vector<casacore::Double> world = _coord_sys.referenceValue().copy();
    casacore::Double w0 = cos(pa) * length.getValue(unit);
    casacore::Double w1 = sin(pa) * length.getValue(unit);
    world(worldAxis0) += w0;
    world(worldAxis1) += w1;

    casacore::Vector<casacore::Double> pixel;
    ThrowIf(!_coord_sys.toPixel(pixel, world), _coord_sys.errorMessage());

    return hypot(pixel(pixel_axes(0)), pixel(pixel_axes(1)));
}

casacore::Vector<casacore::Double> Deconvolver::WidthToCartesian(const casacore::Quantity& width, const casacore::Quantity& pa,
    const casacore::MDirection& dir_ref, const casacore::Vector<casacore::Double>& pixel_center) {
    // Find MDirection of tip of axis
    casacore::MDirection dir_tip = dir_ref;
    dir_tip.shiftAngle(width, pa);

    // Convert to pixel
    auto dir_coord = _coord_sys.directionCoordinate();
    casacore::Vector<casacore::Double> pixel_tip(2);
    if (!dir_coord.toPixel(pixel_tip, dir_tip)) {
        spdlog::error("Direction coordinate conversion to pixel failed: {}", dir_coord.errorMessage());
    }

    // Find offset cartesian components
    casacore::Vector<casacore::Double> cart(2);
    cart(0) = pixel_tip(0) - pixel_center(0);
    cart(1) = pixel_tip(1) - pixel_center(1);
    return cart;
}

casacore::Vector<casacore::Double> Deconvolver::ToPixel(
    casacore::MDirection md_world, casacore::Quantity major_world, casacore::Quantity minor_world, casacore::Quantity pa_major) {
    casacore::Vector<casacore::Double> parameters(5);
    casacore::Vector<casacore::Double> pixel_center;
    auto dir_coord = _coord_sys.directionCoordinate();
    dir_coord.toPixel(pixel_center, md_world);
    parameters(0) = pixel_center(0);
    parameters(1) = pixel_center(1);

    // Convert the tip of the major axis to x/y pixel coordinates
    major_world.scale(0.5);
    casacore::Vector<casacore::Double> major_cart = WidthToCartesian(major_world, pa_major, md_world, pixel_center);

    // Position angle of major axis. atan2 gives pos +x (long) -> +y (lat). put in range +/- pi
    casacore::MVAngle pa(atan2(major_cart(1), major_cart(0)));
    casacore::Quantity pa_minor = pa_major + casacore::Quantity(casacore::C::pi / 2.0, casacore::Unit("rad"));
    casacore::Double dx = sin(pa.radian());
    casacore::Double dy = cos(pa.radian());
    casacore::Vector<casacore::Double> pos_pix = pixel_center.copy();
    casacore::MDirection pos_world;
    casacore::MVDirection mvd_ref = md_world.getValue();
    casacore::Vector<casacore::Double> pre_pos_pix(2);
    minor_world.scale(0.5);
    casacore::Double minor_world_rad = minor_world.getValue(casacore::Unit("rad"));
    casacore::Double sep = 0.0;
    casacore::Double pre_sep = 0.0;

    casacore::Bool more(true);
    while (more) {
        dir_coord.toWorld(pos_world, pos_pix);
        casacore::MVDirection mvd = pos_world.getValue();
        sep = mvd_ref.separation(mvd);
        if (sep > minor_world_rad) {
            break;
        }
        pre_pos_pix = pos_pix;
        pre_sep = sep;
        pos_pix(0) += dx;
        pos_pix(1) += dy;
    }

    casacore::Double frac = (minor_world_rad - pre_sep) / (sep - pre_sep);
    casacore::Double frac_x = dx * frac;
    casacore::Double frac_y = dy * frac;

    casacore::Vector<casacore::Double> minor_cart(2);
    minor_cart(0) = pre_pos_pix(0) + frac_x - pixel_center(0);
    minor_cart(1) = pre_pos_pix(1) + frac_y - pixel_center(1);
    casacore::Double tmp1 = 2.0 * hypot(major_cart(0), major_cart(1));
    casacore::Double tmp2 = 2.0 * hypot(minor_cart(0), minor_cart(1));

    parameters(2) = casacore::max(tmp1, tmp2);
    parameters(3) = casacore::min(tmp1, tmp2);
    parameters(4) = pa.radian();
    return parameters;
}

} // namespace carta
