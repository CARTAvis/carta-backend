/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_

#include <casacode/components/ComponentModels/ComponentList.h>
#include <casacode/components/ComponentModels/GaussianDeconvolver.h>
#include <casacode/components/ComponentModels/SkyComponentFactory.h>

namespace carta {

template <class T>
Deconvolver<T>::Deconvolver(casa::SPIIF image) : _image(image) {}

template <class T>
bool Deconvolver<T>::DoDeconvolution(
    int chan, int stokes, const CARTA::GaussianComponent& in_gauss, std::shared_ptr<casa::GaussianShape>& out_gauss) {
    bool success(false);
    casacore::Vector<casacore::Double> gauss_param(6, 0);
    gauss_param[0] = in_gauss.amp();
    gauss_param[1] = in_gauss.center().x();
    gauss_param[2] = in_gauss.center().y();
    gauss_param[3] = in_gauss.fwhm().x();
    gauss_param[4] = in_gauss.fwhm().y();
    gauss_param[5] = in_gauss.pa();

    casacore::IPosition image_shape = _image->shape();
    casacore::IPosition start_pos(image_shape.nelements(), 0);
    casacore::IPosition end_pos(image_shape - 1);
    casacore::IPosition stride(image_shape.nelements(), 1);

    const casacore::CoordinateSystem& image_coord_sys = _image->coordinates();
    if (image_coord_sys.hasSpectralAxis()) {
        casacore::uInt spectral_axis_number = image_coord_sys.spectralAxisNumber();
        start_pos[spectral_axis_number] = chan;
        end_pos[spectral_axis_number] = start_pos[spectral_axis_number];
    }

    casacore::String stokes_str("I"); // Default stokes string is "I"
    if (image_coord_sys.hasPolarizationCoordinate()) {
        casacore::uInt stokesAxisNumber = image_coord_sys.polarizationAxisNumber();
        start_pos[stokesAxisNumber] = stokes;
        end_pos[stokesAxisNumber] = start_pos[stokesAxisNumber];

        // Get stokes string if any
        casacore::String iquv("IQUV");
        for (auto c : iquv) {
            casacore::String tmp_stokes_str = casacore::String(c);
            auto tmp_stokes_idx = image_coord_sys.stokesPixelNumber(tmp_stokes_str);
            if (tmp_stokes_idx == stokes) {
                stokes_str = tmp_stokes_str;
            }
        }
    }

    casacore::Slicer slice(start_pos, end_pos, stride, casacore::Slicer::endIsLast);
    casacore::SubImage<T> all_axes_sub_image = casacore::SubImage<T>(*_image, slice, false, casacore::AxesSpecifier(true));
    casacore::SubImage<T> sub_image = casacore::SubImage<T>(all_axes_sub_image, casacore::AxesSpecifier(false));
    const casacore::CoordinateSystem& coord_sys = sub_image.coordinates();
    casacore::Bool is_longitude = coord_sys.isDirectionAbscissaLongitude();
    casacore::GaussianBeam beam = _image->imageInfo().restoringBeam(chan, stokes);

    casacore::Stokes::StokesTypes stokes_type = casacore::Stokes::type(stokes_str);
    casacore::Bool deconvolve(false);
    casacore::Double fac_to_Jy;
    casa::SkyComponent sky_comp;
    casa::ComponentType::Shape model_type = casa::ComponentType::Shape::GAUSSIAN;
    std::shared_ptr<casacore::LogIO> log = std::make_shared<casacore::LogIO>(casacore::LogIO());

    try {
        sky_comp = casa::SkyComponentFactory::encodeSkyComponent(
            *log, fac_to_Jy, all_axes_sub_image, model_type, gauss_param, stokes_type, is_longitude, deconvolve, beam);
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

    casa::ComponentList comp_list = casa::ComponentList();
    comp_list.add(sky_comp.copy());
    const auto* ori_gauss_shape = static_cast<const casa::GaussianShape*>(comp_list.getShape(0));
    casacore::Quantity ori_major = ori_gauss_shape->majorAxis();
    casacore::Quantity ori_minor = ori_gauss_shape->minorAxis();
    casacore::Quantity ori_pa = ori_gauss_shape->positionAngle();

    casacore::Bool fit_success = false;
    casacore::GaussianBeam best_sol(ori_major, ori_minor, ori_pa);
    casacore::GaussianBeam best_decon_sol;
    casacore::Bool is_point_source = true;

    // Get deconvolved gaussian
    try {
        is_point_source = casa::GaussianDeconvolver::deconvolve(best_decon_sol, best_sol, beam);
        fit_success = true;
    } catch (const casacore::AipsError& x) {
        fit_success = false;
        is_point_source = true;
    }

    // Calculate errors for deconvolved gaussian from original fit results
    double base_fac = casacore::C::sqrt2 / CorrelatedOverallSNR(chan, stokes, ori_major, ori_minor, 0.5, 2.5);
    double ori_major_val = ori_major.getValue("arcsec");
    double ori_minor_val = ori_minor.getValue("arcsec");
    casacore::Quantum<double> err_pa =
        ori_major_val == ori_minor_val
            ? casacore::QC::qTurn()
            : casacore::Quantity(base_fac * casacore::C::sqrt2 *
                                     (ori_major_val * ori_minor_val / (ori_major_val * ori_major_val - ori_minor_val * ori_minor_val)),
                  "rad");
    err_pa.convert(ori_pa);

    casacore::Quantum<double> err_major =
        casacore::C::sqrt2 / CorrelatedOverallSNR(chan, stokes, ori_major, ori_minor, 2.5, 0.5) * ori_major;
    casacore::Quantum<double> err_minor =
        casacore::C::sqrt2 / CorrelatedOverallSNR(chan, stokes, ori_major, ori_minor, 0.5, 2.5) * ori_minor;
    casacore::GaussianBeam decon_beam;

    // Set deconvolved results
    out_gauss.reset(static_cast<casa::GaussianShape*>(comp_list.getShape(0)->clone()));

    if (fit_success) {
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
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    my_major = max(major_range[i], minor_range[j]);
                    my_minor = min(major_range[i], minor_range[j]);
                    if (my_major.getValue() > 0 && my_minor.getValue() > 0) {
                        source_in.setMajorMinor(my_major, my_minor);

                        for (int k = 0; k < 2; k++) {
                            source_in.setPA(pa_range[k]);
                            decon_beam = casacore::GaussianBeam();
                            casacore::Bool is_point;

                            try {
                                is_point = casa::GaussianDeconvolver::deconvolve(decon_beam, source_in, beam);
                            } catch (const casacore::AipsError& x) {
                                is_point = true;
                            }

                            if (!is_point) {
                                casacore::Quantity tmp_err_major = abs(best_decon_sol.getMajor() - decon_beam.getMajor());
                                tmp_err_major.convert(err_major.getUnit());

                                casacore::Quantity tmp_err_minor = abs(best_decon_sol.getMinor() - decon_beam.getMinor());
                                tmp_err_minor.convert(err_minor.getUnit());

                                casacore::Quantity tmp_err_pa = abs(best_decon_sol.getPA(true) - decon_beam.getPA(true));
                                tmp_err_pa = min(tmp_err_pa, abs(tmp_err_pa - casacore::QC::hTurn()));
                                tmp_err_pa.convert(err_pa.getUnit());

                                err_major = max(err_major, tmp_err_major);
                                err_minor = max(err_minor, tmp_err_minor);
                                err_pa = max(err_pa, tmp_err_pa);
                            }
                        }
                    }
                }
            }
            out_gauss->setWidth(best_decon_sol.getMajor(), best_decon_sol.getMinor(), best_decon_sol.getPA(false));
            out_gauss->setErrors(err_major, err_minor, err_pa);
            success = true;
        }
    }
    return success;
}

template <class T>
double Deconvolver<T>::GetResidueRms() {
    return 1.43619; // In the unit of arcsec. Todo: this value is calculated from the original fit result
}

template <class T>
casacore::Quantity Deconvolver<T>::GetNoiseFWHM(int chan, int stokes) {
    casacore::GaussianBeam beam = _image->imageInfo().restoringBeam(chan, stokes);
    return casacore::Quantity(sqrt(beam.getMajor() * beam.getMinor()).get("arcsec"));
}

template <class T>
double Deconvolver<T>::CorrelatedOverallSNR(int chan, int stokes, casacore::Quantity major, casacore::Quantity minor, double a, double b) {
    casacore::Quantity noise_FWHM = GetNoiseFWHM(chan, stokes);
    casacore::Quantity peak_intensities = casacore::Quantity(77.8518, "Jy/beam.km/s"); // Todo: this value is from the original fit result
    double signal_to_noise = abs(peak_intensities).getValue() / GetResidueRms();
    double fac = signal_to_noise / 2 * (sqrt(major * minor) / (noise_FWHM)).getValue("");
    double p = (noise_FWHM / major).getValue("");
    double fac1 = pow(1 + p * p, a / 2);
    double q = (noise_FWHM / minor).getValue("");
    double fac2 = pow(1 + q * q, b / 2);
    return fac * fac1 * fac2;
}

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_
