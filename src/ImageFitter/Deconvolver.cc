/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Deconvolver.h"

#include <casacode/components/ComponentModels/GaussianDeconvolver.h>
#include <casacode/components/ComponentModels/SkyComponentFactory.h>

namespace carta {

Deconvolver::Deconvolver(
    casacore::CoordinateSystem coord_sys, casacore::Unit brightness_unit, casacore::GaussianBeam beam, int stokes, double residue_rms)
    : _coord_sys(coord_sys), _brightness_unit(brightness_unit), _beam(beam), _stokes(stokes), _residue_rms(residue_rms) {
    _noise_FWHM = casacore::Quantity(casacore::sqrt(_beam.getMajor() * _beam.getMinor()).get("arcsec"));
}

std::string Deconvolver::GetDeconvolutionLog(const std::vector<CARTA::GaussianComponent>& in_gauss_vec) {
    std::ostringstream log;
    log << "\n--- Deconvolved from beam ---\n";
    for (int i = 0; i < in_gauss_vec.size(); ++i) {
        const CARTA::GaussianComponent& in_gauss = in_gauss_vec[i];
        DeconvolutionResult result;
        if (DoDeconvolution(in_gauss, result)) {
            log << fmt::format("Component #{}:\n", i + 1);
            std::string major = fmt::format("{:.6f}", result.major.getValue());
            std::string minor = fmt::format("{:.6f}", result.minor.getValue());
            std::string pa = fmt::format("{:.6f}", result.pa.getValue());

            std::string err_major = fmt::format("{:.6f}", result.major_err.getValue());
            std::string err_minor = fmt::format("{:.6f}", result.minor_err.getValue());
            std::string err_pa = fmt::format("{:.6f}", result.pa_err.getValue());

            std::string unit_major = result.major.getUnit();
            std::string unit_minor = result.minor.getUnit();
            std::string unit_pa = result.pa.getUnit();

            casacore::Vector<casacore::Double> pixel_params;
            bool pixel_params_available = WorldWidthToPixel(result.major, result.minor, result.pa, pixel_params);

            log << fmt::format("FWHM Major Axis = {} +/- {} ({})\n", major, err_major, unit_major);
            if (pixel_params_available) {
                log << fmt::format("                = {:.6f} +/- {} (pix)\n", pixel_params(0), 0);
            }
            log << fmt::format("FWHM Minor Axis = {} +/- {} ({})\n", minor, err_minor, unit_minor);
            if (pixel_params_available) {
                log << fmt::format("                = {:.6f} +/- {} (pix)\n", pixel_params(1), 0);
            }
            log << fmt::format("P.A.            = {} +/- {} ({})\n", pa, err_pa, unit_pa);
        }
    }
    return log.str();
}

bool Deconvolver::DoDeconvolution(const CARTA::GaussianComponent& in_gauss, DeconvolutionResult& result) {
    bool success(false);
    casacore::Vector<casacore::Double> gauss_param(6, 0);
    gauss_param[0] = in_gauss.amp();
    gauss_param[1] = in_gauss.center().x();
    gauss_param[2] = in_gauss.center().y();
    gauss_param[3] = in_gauss.fwhm().x();
    gauss_param[4] = in_gauss.fwhm().y();
    gauss_param[5] = in_gauss.pa(); // in the unit of *degree*

    // Rotate 90 degrees and convert the unit of position angle to *rad*
    gauss_param[5] = (gauss_param[5] + 90.0) * casacore::C::pi / 180.0;

    // Get stokes string if any, the default stokes string is "I"
    casacore::String stokes_str("I");
    if (_coord_sys.hasPolarizationCoordinate()) {
        casacore::String iquv("IQUV");
        for (auto c : iquv) {
            casacore::String tmp_stokes_str = casacore::String(c);
            auto tmp_stokes = _coord_sys.stokesPixelNumber(tmp_stokes_str);
            if (tmp_stokes == _stokes) {
                stokes_str = tmp_stokes_str;
            }
        }
    }

    casacore::Bool x_is_longitude = _coord_sys.isDirectionAbscissaLongitude();
    casacore::Stokes::StokesTypes stokes_type = casacore::Stokes::type(stokes_str);
    casacore::Double fac_to_Jy;
    casa::ComponentType::Shape model_type = casa::ComponentType::Shape::GAUSSIAN;
    std::shared_ptr<casacore::LogIO> log = std::make_shared<casacore::LogIO>(casacore::LogIO());
    casa::SkyComponent sky_comp;

    try {
        sky_comp = casa::SkyComponentFactory::encodeSkyComponent(
            *log, fac_to_Jy, _coord_sys, _brightness_unit, model_type, gauss_param, stokes_type, x_is_longitude, _beam);
    } catch (const casacore::AipsError& x) {
        std::string solution_str = "[";
        for (int i = 0; i < gauss_param.size(); ++i) {
            solution_str += gauss_param[i];
            if (i != gauss_param.size() - 1) {
                solution_str += ", ";
            }
        }
        solution_str += "]";

        spdlog::error(
            "Fit converged but transforming fit in pixel to world coordinates failed. "
            "Fit may be nonsensical, especially if any of the following fitted values are extremely large: {}. The lower level exception "
            "message is {}",
            solution_str, x.getMesg());
        return success;
    }

    auto* ori_gauss_shape = static_cast<const casa::GaussianShape*>(sky_comp.shape().clone());
    casacore::Quantity ori_major = ori_gauss_shape->majorAxis();
    casacore::Quantity ori_minor = ori_gauss_shape->minorAxis();
    casacore::Quantity ori_pa = ori_gauss_shape->positionAngle();
    casacore::GaussianBeam best_sol(ori_major, ori_minor, ori_pa);
    casacore::GaussianBeam best_decon_sol;
    casacore::Bool is_point_source(true);

    // Get deconvolved gaussian
    try {
        is_point_source = casa::GaussianDeconvolver::deconvolve(best_decon_sol, best_sol, _beam);
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
                                is_point = casa::GaussianDeconvolver::deconvolve(decon_beam, source_in, _beam);
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
            result = {best_decon_sol.getMajor(), best_decon_sol.getMinor(), best_decon_sol.getPA(true), err_major, err_minor, err_pa};
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

bool Deconvolver::WorldWidthToPixel(
    casacore::Quantity major, casacore::Quantity minor, casacore::Quantity pa, casacore::Vector<casacore::Double>& pixel_params) {
    casacore::Vector<casacore::Quantity> world_params(5);
    world_params(0).setValue(0);
    world_params(0).setUnit(casacore::String(""));
    world_params(1).setValue(0);
    world_params(1).setUnit(casacore::String(""));
    world_params(2) = major;
    world_params(3) = minor;
    world_params(4) = pa;

    casacore::IPosition pixelAxes = {0, 1};
    casacore::Bool do_ref(true);
    try {
        casa::SkyComponentFactory::worldWidthsToPixel(pixel_params, world_params, _coord_sys, pixelAxes, do_ref);
    } catch (const casacore::AipsError& x) {
        spdlog::error("Fail to convert 2D Gaussian world width to pixel {}", x.getMesg());
        return false;
    }
    return true;
}

} // namespace carta
