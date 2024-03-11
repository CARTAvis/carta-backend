/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_
#define CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_

#include <imageanalysis/ImageAnalysis/ImageFitter.h>

#include <casacore/casa/BasicSL/STLIO.h>
#include <casacore/casa/Utilities/Precision.h>
#include <components/ComponentModels/ComponentShape.h>
#include <components/ComponentModels/Flux.h>
#include <components/ComponentModels/GaussianDeconvolver.h>
#include <components/ComponentModels/GaussianShape.h>
#include <components/ComponentModels/PointShape.h>
#include <components/ComponentModels/SkyComponentFactory.h>
#include <components/ComponentModels/SpectralModel.h>

#include <casacore/lattices/LRegions/LCPixelSet.h>

#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/IO/FitterEstimatesFileParser.h>
#include <imageanalysis/ImageAnalysis/ImageStatsCalculator.h>
#include <imageanalysis/ImageAnalysis/PeakIntensityFluxDensityConverter.h>

using namespace casacore;

namespace carta {

template <class T>
const String Deconvolver<T>::_class = "Deconvolver";

template <class T>
Deconvolver<T>::Deconvolver(const SPCIIT image, const String& region, const Record* const& regionRec, const String& box,
    const String& chanInp, const String& stokes, const String& maskInp, const String& estimatesFilename, const String& newEstimatesInp,
    const String& compListName)
    : casa::ImageTask<T>(image, region, regionRec, box, chanInp, stokes, maskInp, "", false) {
    if (stokes.empty() && image->coordinates().hasPolarizationCoordinate() && regionRec == 0 && region.empty()) {
        const CoordinateSystem& csys = image->coordinates();
        casacore::Int pol_axis_number = csys.polarizationAxisNumber();
        casacore::Int stokesVal = (casacore::Int)csys.toWorld(casacore::IPosition(image->ndim(), 0))[pol_axis_number];
        this->_setStokes(Stokes::name(Stokes::type(stokesVal)));
    }
    this->_construct();
}

template <class T>
vector<Coordinate::Type> Deconvolver<T>::_getNecessaryCoordinates() const {
    vector<Coordinate::Type> coordType(1);
    coordType[0] = Coordinate::DIRECTION;
    return coordType;
}

template <class T>
casa::CasacRegionManager::StokesControl Deconvolver<T>::_getStokesControl() const {
    return casa::CasacRegionManager::USE_FIRST_STOKES;
}

template <class T>
bool Deconvolver<T>::DoDeconvolution(const CARTA::GaussianComponent& in_gauss) {
    bool success(false);
    casacore::Vector<casacore::Double> gauss_param(6, 0);
    gauss_param[0] = in_gauss.amp();
    gauss_param[1] = in_gauss.center().x();
    gauss_param[2] = in_gauss.center().y();
    gauss_param[3] = in_gauss.fwhm().x();
    gauss_param[4] = in_gauss.fwhm().y();
    gauss_param[5] = in_gauss.pa();

    auto sub_image_tmp = casa::SubImageFactory<T>::createImage(
        *this->_getImage(), "", *this->_getRegion(), this->_getMask(), false, false, false, this->_getStretch());

    casacore::IPosition image_shape = sub_image_tmp->shape();
    casacore::IPosition start_pos(image_shape.nelements(), 0);
    casacore::IPosition end_pos(image_shape - 1);
    casacore::IPosition stride(image_shape.nelements(), 1);

    const CoordinateSystem& image_coord_sys = sub_image_tmp->coordinates();
    if (image_coord_sys.hasSpectralAxis()) {
        int chan = 0;
        uInt spectral_axis_number = image_coord_sys.spectralAxisNumber();
        start_pos[spectral_axis_number] = chan;
        end_pos[spectral_axis_number] = start_pos[spectral_axis_number];
    }

    if (image_coord_sys.hasPolarizationCoordinate()) {
        casacore::String stokes = "I";
        uInt stokesAxisNumber = image_coord_sys.polarizationAxisNumber();
        start_pos[stokesAxisNumber] = image_coord_sys.stokesPixelNumber(stokes);
        end_pos[stokesAxisNumber] = start_pos[stokesAxisNumber];
    }

    casacore::Slicer slice(start_pos, end_pos, stride, Slicer::endIsLast);
    casacore::SubImage<T> all_axes_sub_image = casacore::SubImage<T>(*sub_image_tmp, slice, false, casacore::AxesSpecifier(true));
    casacore::SubImage<T> sub_image = casacore::SubImage<T>(all_axes_sub_image, casacore::AxesSpecifier(false));
    const casacore::CoordinateSystem& coord_sys = sub_image.coordinates();
    casacore::Bool is_longitude = coord_sys.isDirectionAbscissaLongitude();
    int chan = 0;
    int stokes_idx = 0;
    casacore::GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(chan, stokes_idx);

    casacore::Stokes::StokesTypes stokes = Stokes::type("I");
    casacore::Bool deconvolve = false;
    casa::SkyComponent sky_comp;
    casacore::Double fac_to_Jy;
    casa::ComponentType::Shape model_type = casa::ComponentType::Shape::GAUSSIAN;

    try {
        sky_comp = casa::SkyComponentFactory::encodeSkyComponent(
            *this->_getLog(), fac_to_Jy, all_axes_sub_image, model_type, gauss_param, stokes, is_longitude, deconvolve, beam);
    } catch (const AipsError& x) {
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
    casacore::Quantity ori_maj = ori_gauss_shape->majorAxis();
    casacore::Quantity ori_minor = ori_gauss_shape->minorAxis();
    casacore::Quantity ori_pa = ori_gauss_shape->positionAngle();

    casacore::Bool fit_success = false;
    casacore::GaussianBeam best_sol(ori_maj, ori_minor, ori_pa);
    casacore::GaussianBeam best_decon;
    casacore::Bool is_point_source = true;

    try {
        is_point_source = casa::GaussianDeconvolver::deconvolve(best_decon, best_sol, beam);
        fit_success = true;
    } catch (const AipsError& x) {
        fit_success = false;
        is_point_source = true;
    }

    // Calculate errors from original fit results
    double base_fac = casacore::C::sqrt2 / CorrelatedOverallSNR(ori_maj, ori_minor, 0.5, 2.5);
    double ori_maj_val = ori_maj.getValue("arcsec");
    double ori_minor_val = ori_minor.getValue("arcsec");
    casacore::Quantum<double> epa =
        ori_maj_val == ori_minor_val
            ? casacore::QC::qTurn()
            : casacore::Quantity(base_fac * casacore::C::sqrt2 *
                                     (ori_maj_val * ori_minor_val / (ori_maj_val * ori_maj_val - ori_minor_val * ori_minor_val)),
                  "rad");
    epa.convert(ori_pa);

    casacore::Quantum<double> emajor = casacore::C::sqrt2 / CorrelatedOverallSNR(ori_maj, ori_minor, 2.5, 0.5) * ori_maj;
    casacore::Quantum<double> eminor = casacore::C::sqrt2 / CorrelatedOverallSNR(ori_maj, ori_minor, 0.5, 2.5) * ori_minor;

    // Calculate the errors for deconvolved fit results
    std::shared_ptr<casa::PointShape> point_shape;
    casacore::GaussianBeam decon_beam;
    std::shared_ptr<casa::GaussianShape> gauss_shape(static_cast<casa::GaussianShape*>(comp_list.getShape(0)->clone()));

    if (fit_success) {
        if (is_point_source) {
            static const casacore::Quantity tiny(1e-60, "arcsec");
            static const casacore::Quantity zero(0, "deg");
            gauss_shape->setWidth(tiny, tiny, zero);
            casacore::Quantity maj = best_decon.getMajor();
            casacore::Quantity minor = best_decon.getMinor();
            casacore::Quantity pa = best_decon.getPA(false);

            casacore::GaussianBeam largest(maj + emajor, minor + eminor, pa - epa);
            casacore::Bool is_point_source1 = true;
            try {
                is_point_source1 = casa::GaussianDeconvolver::deconvolve(decon_beam, largest, beam);
                fit_success = true;
            } catch (const casacore::AipsError& x) {
                is_point_source1 = true;
            }
            casacore::GaussianBeam lsize;
            if (!is_point_source1) {
                lsize = decon_beam;
            }

            largest.setPA(pa + epa);
            casacore::Bool is_point_source2 = true;
            try {
                is_point_source2 = casa::GaussianDeconvolver::deconvolve(decon_beam, largest, beam);
            } catch (const AipsError& x) {
                is_point_source2 = true;
            }

            if (is_point_source2) {
                if (is_point_source1) {
                    point_shape.reset(new casa::PointShape());
                    point_shape->copyDirectionInfo(*gauss_shape);
                } else {
                    gauss_shape->setErrors(lsize.getMajor(), lsize.getMinor(), zero);
                }
            } else {
                if (is_point_source1) {
                    gauss_shape->setErrors(decon_beam.getMajor(), decon_beam.getMinor(), zero);
                } else {
                    Quantity lmaj = max(decon_beam.getMajor(), lsize.getMajor());
                    Quantity lmin = max(decon_beam.getMinor(), lsize.getMinor());
                    gauss_shape->setErrors(lmaj, lmin, zero);
                }
            }
        } else {
            casacore::Vector<casacore::Quantity> maj_range(2, ori_maj - emajor);
            maj_range[1] = ori_maj + emajor;
            casacore::Vector<casacore::Quantity> min_range(2, ori_minor - eminor);
            min_range[1] = ori_minor + eminor;
            casacore::Vector<casacore::Quantity> pa_range(2, ori_pa - epa);
            pa_range[1] = ori_pa + epa;
            casacore::GaussianBeam source_in;
            casacore::Quantity my_major, my_minor;
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    my_major = max(maj_range[i], min_range[j]);
                    my_minor = min(maj_range[i], min_range[j]);
                    if (my_major.getValue() > 0 && my_minor.getValue() > 0) {
                        source_in.setMajorMinor(my_major, my_minor);
                        for (int k = 0; k < 2; k++) {
                            source_in.setPA(pa_range[k]);
                            decon_beam = casacore::GaussianBeam();
                            casacore::Bool is_point;
                            try {
                                is_point = casa::GaussianDeconvolver::deconvolve(decon_beam, source_in, beam);
                            } catch (const AipsError& x) {
                                is_point = true;
                            }
                            if (!is_point) {
                                Quantity tmp_emaj = abs(best_decon.getMajor() - decon_beam.getMajor());
                                tmp_emaj.convert(emajor.getUnit());
                                Quantity tmp_eminor = abs(best_decon.getMinor() - decon_beam.getMinor());
                                tmp_eminor.convert(eminor.getUnit());
                                Quantity tmp_epa = abs(best_decon.getPA(true) - decon_beam.getPA(true));
                                tmp_epa = min(tmp_epa, abs(tmp_epa - casacore::QC::hTurn()));
                                tmp_epa.convert(epa.getUnit());
                                emajor = max(emajor, tmp_emaj);
                                eminor = max(eminor, tmp_eminor);
                                epa = max(epa, tmp_epa);
                            }
                        }
                    }
                }
            }
            gauss_shape->setWidth(best_decon.getMajor(), best_decon.getMinor(), best_decon.getPA(false));
            gauss_shape->setErrors(emajor, eminor, epa);
            std::cout << " --- major axis FWHM = " << gauss_shape->majorAxis() << " +/- " << gauss_shape->majorAxisError() << "\n";
            std::cout << " --- minor axis FWHM = " << gauss_shape->minorAxis() << " +/- " << gauss_shape->minorAxisError() << "\n";
            std::cout << " --- position angle = " << gauss_shape->positionAngle() << " +/- " << gauss_shape->positionAngleError() << "\n";
        }
        success = true;
    } else {
        point_shape.reset(new casa::PointShape());
        point_shape->copyDirectionInfo(*gauss_shape);
    }
    return success;
}

template <class T>
double Deconvolver<T>::GetResidueRms() {
    return 1.43619; // In the unit of arcsec. Todo: this value is calculated from the original fit result
}

template <class T>
casacore::Quantity Deconvolver<T>::GetNoiseFWHM() {
    int chan = 0;
    int stokes = 0;
    casacore::GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(chan, stokes);
    return casacore::Quantity(sqrt(beam.getMajor() * beam.getMinor()).get("arcsec"));
}

template <class T>
double Deconvolver<T>::CorrelatedOverallSNR(Quantity maj, Quantity minor, double a, double b) {
    casacore::Quantity noise_FWHM = GetNoiseFWHM();
    casacore::Quantity peak_intensities = casacore::Quantity(77.8518, "Jy/beam.km/s"); // Todo: this value is from the original fit result
    double signal_to_noise = abs(peak_intensities).getValue() / GetResidueRms();
    double fac = signal_to_noise / 2 * (sqrt(maj * minor) / (noise_FWHM)).getValue("");
    double p = (noise_FWHM / maj).getValue("");
    double fac1 = pow(1 + p * p, a / 2);
    double q = (noise_FWHM / minor).getValue("");
    double fac2 = pow(1 + q * q, b / 2);
    return fac * fac1 * fac2;
}

} // namespace carta

#endif // CARTA_SRC_IMAGEFITTER_DECONVOLVER_TCC_
