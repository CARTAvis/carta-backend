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
    : casa::ImageTask<T>(image, region, regionRec, box, chanInp, stokes, maskInp, "", false),
      _regionString(region),
      _newEstimatesFileName(newEstimatesInp),
      _compListName(compListName),
      _bUnit(image->units().getName()),
      _correlatedNoise(image->imageInfo().hasBeam()),
      _zeroLevelOffsetSolution(0),
      _zeroLevelOffsetError(0),
      _results(image, this->_getLog()) {
    if (stokes.empty() && image->coordinates().hasPolarizationCoordinate() && regionRec == 0 && region.empty()) {
        const CoordinateSystem& csys = image->coordinates();
        Int polAxis = csys.polarizationAxisNumber();
        Int stokesVal = (Int)csys.toWorld(IPosition(image->ndim(), 0))[polAxis];
        this->_setStokes(Stokes::name(Stokes::type(stokesVal)));
    }
    this->_construct();
}

template <class T>
Deconvolver<T>::~Deconvolver() {}

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
    casacore::Vector<casacore::Double> solution(6, 0);
    solution[0] = in_gauss.amp();
    solution[1] = in_gauss.center().x();
    solution[2] = in_gauss.center().y();
    solution[3] = in_gauss.fwhm().x();
    solution[4] = in_gauss.fwhm().y();
    solution[5] = in_gauss.pa();

    casa::SkyComponent result;
    casacore::Double fac_to_Jy;
    casacore::SubImage<T> all_axes_sub_image;

    auto sub_image_tmp = casa::SubImageFactory<T>::createImage(
        *this->_getImage(), "", *this->_getRegion(), this->_getMask(), false, false, false, this->_getStretch());

    {
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
        all_axes_sub_image = casacore::SubImage<T>(*sub_image_tmp, slice, false, casacore::AxesSpecifier(true));
    }

    casa::ComponentType::Shape model_type = casa::ComponentType::Shape::GAUSSIAN;
    casacore::Stokes::StokesTypes stokes = Stokes::type("I");
    casacore::SubImage<T> sub_image = casacore::SubImage<T>(all_axes_sub_image, casacore::AxesSpecifier(false));
    Bool deconvolve = false;

    const casacore::CoordinateSystem& coord_sys = sub_image.coordinates();
    casacore::Bool is_longitude = coord_sys.isDirectionAbscissaLongitude();
    int chan = 0;
    int stokes_idx = 0;
    casacore::GaussianBeam beam = this->_getImage()->imageInfo().restoringBeam(chan, stokes_idx);

    try {
        result = casa::SkyComponentFactory::encodeSkyComponent(
            *this->_getLog(), fac_to_Jy, all_axes_sub_image, model_type, solution, stokes, is_longitude, deconvolve, beam);
    } catch (const AipsError& x) {
        std::string solution_str = "[";
        for (int i = 0; i < solution.size(); ++i) {
            solution_str += solution[i];
            if (i != solution.size() - 1) {
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

    casa::ComponentList cur_convolved_list = casa::ComponentList();
    cur_convolved_list.add(result.copy());
    const auto* ori_gauss_shape = static_cast<const casa::GaussianShape*>(cur_convolved_list.getShape(0));
    casacore::Quantity ori_maj = ori_gauss_shape->majorAxis();
    casacore::Quantity ori_minor = ori_gauss_shape->minorAxis();
    casacore::Quantity ori_pa = ori_gauss_shape->positionAngle();

    std::shared_ptr<casa::GaussianShape> gauss_shape(static_cast<casa::GaussianShape*>(cur_convolved_list.getShape(0)->clone()));

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
    casacore::Quantity maj = best_decon.getMajor();
    casacore::Quantity minor = best_decon.getMinor();
    casacore::Quantity pa = best_decon.getPA(false);

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

    casacore::Quantum<double> emaj = casacore::C::sqrt2 / CorrelatedOverallSNR(ori_maj, ori_minor, 2.5, 0.5) * ori_maj;
    casacore::Quantum<double> emin = casacore::C::sqrt2 / CorrelatedOverallSNR(ori_maj, ori_minor, 0.5, 2.5) * ori_minor;

    // Calculate the errors for deconvolved fit results
    std::shared_ptr<casa::PointShape> point;
    casacore::GaussianBeam decon;

    if (fit_success) {
        if (is_point_source) {
            static const casacore::Quantity tiny(1e-60, "arcsec");
            static const casacore::Quantity zero(0, "deg");
            gauss_shape->setWidth(tiny, tiny, zero);

            casacore::GaussianBeam largest(maj + emaj, minor + emin, pa - epa);
            casacore::Bool is_point_source1 = true;
            try {
                is_point_source1 = casa::GaussianDeconvolver::deconvolve(decon, largest, beam);
                fit_success = true;
            } catch (const casacore::AipsError& x) {
                is_point_source1 = true;
            }
            casacore::GaussianBeam lsize;
            if (!is_point_source1) {
                lsize = decon;
            }

            largest.setPA(pa + epa);
            casacore::Bool is_point_source2 = true;
            try {
                is_point_source2 = casa::GaussianDeconvolver::deconvolve(decon, largest, beam);
            } catch (const AipsError& x) {
                is_point_source2 = true;
            }

            if (is_point_source2) {
                if (is_point_source1) {
                    point.reset(new casa::PointShape());
                    point->copyDirectionInfo(*gauss_shape);
                } else {
                    gauss_shape->setErrors(lsize.getMajor(), lsize.getMinor(), zero);
                }
            } else {
                if (is_point_source1) {
                    gauss_shape->setErrors(decon.getMajor(), decon.getMinor(), zero);
                } else {
                    Quantity lmaj = max(decon.getMajor(), lsize.getMajor());
                    Quantity lmin = max(decon.getMinor(), lsize.getMinor());
                    gauss_shape->setErrors(lmaj, lmin, zero);
                }
            }
        } else {
            casacore::Vector<casacore::Quantity> maj_range(2, ori_maj - emaj);
            maj_range[1] = ori_maj + emaj;
            casacore::Vector<casacore::Quantity> min_range(2, ori_minor - emin);
            min_range[1] = ori_minor + emin;
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
                            decon = casacore::GaussianBeam();
                            casacore::Bool is_point;
                            try {
                                is_point = casa::GaussianDeconvolver::deconvolve(decon, source_in, beam);
                            } catch (const AipsError& x) {
                                is_point = true;
                            }
                            if (!is_point) {
                                Quantity errMaj = abs(best_decon.getMajor() - decon.getMajor());
                                errMaj.convert(emaj.getUnit());
                                Quantity errMin = abs(best_decon.getMinor() - decon.getMinor());
                                errMin.convert(emin.getUnit());
                                Quantity errPA = abs(best_decon.getPA(true) - decon.getPA(true));
                                errPA = min(errPA, abs(errPA - QC::hTurn()));
                                errPA.convert(epa.getUnit());
                                emaj = max(emaj, errMaj);
                                emin = max(emin, errMin);
                                epa = max(epa, errPA);
                            }
                        }
                    }
                }
            }
            gauss_shape->setWidth(best_decon.getMajor(), best_decon.getMinor(), best_decon.getPA(false));
            gauss_shape->setErrors(emaj, emin, epa);
            std::cout << " --- major axis FWHM = " << gauss_shape->majorAxis() << " +/- " << gauss_shape->majorAxisError() << "\n";
            std::cout << " --- minor axis FWHM = " << gauss_shape->minorAxis() << " +/- " << gauss_shape->minorAxisError() << "\n";
            std::cout << " --- position angle = " << gauss_shape->positionAngle() << " +/- " << gauss_shape->positionAngleError() << "\n";
        }
        success = true;
    } else {
        point.reset(new casa::PointShape());
        point->copyDirectionInfo(*gauss_shape);
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
