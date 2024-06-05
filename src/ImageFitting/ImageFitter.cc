/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define SQ_FWHM_TO_SIGMA 1.0 / 8.0 / log(2.0)
#define DEG_TO_RAD M_PI / 180.0

#include "ImageFitter.h"
#include "Util/Message.h"

#include <omp.h>

using namespace carta;

ImageFitter::ImageFitter() {
    _fdf.f = FuncF;
    _fdf.df = nullptr; // internally computed using finite difference approximations of f when set to NULL
    _fdf.fvv = nullptr;
    _fdf.params = &_fit_data;

    // avoid GSL default error handler calling abort()
    gsl_set_error_handler(&ErrorHandler);
}

bool ImageFitter::FitImage(size_t width, size_t height, float* image, double beam_size, string unit,
    std::vector<CARTA::GaussianComponent>& initial_values, const std::vector<bool>& fixed_params, double background_offset,
    CARTA::FittingSolverType solver, bool create_model_image, bool create_residual_image, CARTA::FittingResponse& fitting_response,
    GeneratorProgressCallback progress_callback, size_t offset_x, size_t offset_y) {
    bool success = false;
    _fit_data.stop_fitting = false;
    _integrated_flux_values.clear();
    _integrated_flux_errors.clear();
    _model_data.clear();
    _residual_data.clear();

    _fit_data.width = width;
    _fit_data.n = width * height;
    _fit_data.data = image;
    _fit_data.offset_x = offset_x;
    _fit_data.offset_y = offset_y;
    _fdf.n = _fit_data.n;
    _beam_size = beam_size;
    _unit = unit;
    _create_model_data = create_model_image;
    _create_residual_data = create_residual_image;
    _progress_callback = progress_callback;

    CalculateNanNumAndStd();
    success = SetInitialValues(initial_values, background_offset, fixed_params);

    // TODO: allow multiple components with invalid initial value
    if (!success && _num_components > 1) {
        fitting_response.set_message("invalid initial value");
        fitting_response.set_success(success);

        gsl_vector_free(_fit_values);
        gsl_vector_free(_fit_errors);
        return false;
    }

    std::string initialValueLog = "";
    if (!success) {
        success = CalculateInitialValues(initial_values);
        if (success) {
            initialValueLog = InitialValueCalculator::GetLog(initial_values, _unit);
            success = SetInitialValues(initial_values, background_offset, fixed_params);
        }
    }

    if (!success) {
        fitting_response.set_message("error in setting initial values");
        fitting_response.set_success(success);

        gsl_vector_free(_fit_values);
        gsl_vector_free(_fit_errors);
        return false;
    }

    // avoid SolveSystem crashes with insufficient data points
    if (_fit_data.n_notnan < _fit_values->size) {
        fitting_response.set_message("insufficient data points");
        fitting_response.set_success(success);

        gsl_vector_free(_fit_values);
        gsl_vector_free(_fit_errors);
        return false;
    }

    spdlog::info("Fitting image ({} data points) with {} Gaussian component(s) ({} parameter(s)).", _fit_data.n_notnan, _num_components,
        _fit_values->size);
    int status = SolveSystem(solver);

    if (_fit_data.stop_fitting) {
        fitting_response.set_message("task cancelled");
    } else {
        if (status == GSL_EMAXITER && _fit_status.num_iter < _max_iter) {
            fitting_response.set_message("fit did not converge");
        } else if (status) {
            fitting_response.set_message(gsl_strerror(status));
        }

        if (!status || (status == GSL_EMAXITER && _fit_status.num_iter == _max_iter)) {
            success = true;
            spdlog::info("Writing fitting results and log.");
            for (size_t i = 0; i < _num_components; i++) {
                auto values = GetGaussianParams(
                    _fit_values, i * 6, _fit_data.fit_values_indexes, _fit_data.initial_values, _fit_data.offset_x, _fit_data.offset_y);
                fitting_response.add_result_values();
                *fitting_response.mutable_result_values(i) = GetGaussianComponent(values);

                std::vector<double> zeros(6, 0.0);
                auto errors = GetGaussianParams(_fit_errors, i * 6, _fit_data.fit_values_indexes, zeros);
                fitting_response.add_result_errors();
                *fitting_response.mutable_result_errors(i) = GetGaussianComponent(errors);
            }

            if (_integrated_flux_values.size() == _num_components && _integrated_flux_errors.size() == _num_components) {
                for (size_t i = 0; i < _num_components; i++) {
                    fitting_response.add_integrated_flux_values(_integrated_flux_values[i]);
                    fitting_response.add_integrated_flux_errors(_integrated_flux_errors[i]);
                }
            }

            size_t last_index = _fit_data.fit_values_indexes.size() - 1;
            int background_offset_index = _fit_data.fit_values_indexes[last_index];
            double background_offset =
                background_offset_index < 0 ? _fit_data.initial_values[last_index] : gsl_vector_get(_fit_values, background_offset_index);
            double background_offset_error = background_offset_index < 0 ? 0.0 : gsl_vector_get(_fit_errors, background_offset_index);
            fitting_response.set_offset_value(background_offset);
            fitting_response.set_offset_error(background_offset_error);

            fitting_response.set_log(initialValueLog + GetLog());
        }
    }
    fitting_response.set_success(success);

    gsl_vector_free(_fit_values);
    gsl_vector_free(_fit_errors);
    return success;
}

bool ImageFitter::GetGeneratedImages(casa::SPIIF image, const casacore::ImageRegion& image_region, const std::string& filename,
    GeneratedImage& model_image, GeneratedImage& residual_image, CARTA::FittingResponse& fitting_response) {
    if (_create_model_data) {
        model_image = GeneratedImage(GetFilename(filename, "model"), GetImageData(image, image_region, _model_data));
    }
    if (_create_residual_data) {
        residual_image = GeneratedImage(GetFilename(filename, "residual"), GetImageData(image, image_region, _residual_data));
    }
    return true;
}

void ImageFitter::StopFitting() {
    _fit_data.stop_fitting = true;
}

void ImageFitter::CalculateNanNumAndStd() {
    std::vector<double> data_notnan;
    data_notnan.reserve(_fit_data.n);

    _fit_data.n_notnan = _fit_data.n;
    for (size_t i = 0; i < _fit_data.n; i++) {
        if (isnan(_fit_data.data[i])) {
            _fit_data.n_notnan--;
        } else {
            data_notnan.push_back(_fit_data.data[i]);
        }
    }

    _image_std = GetMedianAbsDeviation(_fit_data.n_notnan, data_notnan.data());
    spdlog::debug("MAD = {}", _image_std);
}

bool ImageFitter::SetInitialValues(
    const std::vector<CARTA::GaussianComponent>& initial_values, double background_offset, const std::vector<bool>& fixed_params) {
    _num_components = initial_values.size();
    _fit_data.initial_values.clear();
    for (size_t i = 0; i < _num_components; i++) {
        CARTA::GaussianComponent component(initial_values[i]);
        _fit_data.initial_values.push_back(component.center().x() - _fit_data.offset_x);
        _fit_data.initial_values.push_back(component.center().y() - _fit_data.offset_y);
        _fit_data.initial_values.push_back(component.amp());
        _fit_data.initial_values.push_back(component.fwhm().x());
        _fit_data.initial_values.push_back(component.fwhm().y());
        _fit_data.initial_values.push_back(component.pa());
    }
    _fit_data.initial_values.push_back(std::isnan(background_offset) ? 0 : background_offset);

    _fit_data.fit_values_indexes.clear();
    size_t p;
    if (fixed_params.size() != _fit_data.initial_values.size()) {
        spdlog::warn("Invalid length of the fixed parameter array. Fit with all parameters unfixed except the offset.");
        p = _fit_data.initial_values.size() - 1;
        _fit_values = gsl_vector_alloc(p);
        _fit_errors = gsl_vector_alloc(p);
        for (size_t i = 0; i < p - 1; i++) {
            if (isnan(_fit_data.initial_values[i])) {
                spdlog::info("Found invalid value in the provided initial values.");
                return false;
            }

            _fit_data.fit_values_indexes.push_back(i);
            gsl_vector_set(_fit_values, i, _fit_data.initial_values[i]);
        }
        _fit_data.fit_values_indexes.push_back(-1);
    } else {
        p = std::count(fixed_params.begin(), fixed_params.end(), false);
        _fit_values = gsl_vector_alloc(p);
        _fit_errors = gsl_vector_alloc(p);
        size_t iter = 0;
        for (size_t i = 0; i < fixed_params.size(); i++) {
            if (isnan(_fit_data.initial_values[i])) {
                spdlog::info("Found invalid value in the provided initial values.");
                return false;
            }

            if (!fixed_params[i]) {
                _fit_data.fit_values_indexes.push_back(iter);
                gsl_vector_set(_fit_values, iter, _fit_data.initial_values[i]);
                iter++;
            } else {
                _fit_data.fit_values_indexes.push_back(-1);
            }
        }
    }
    _fdf.p = p;

    return true;
}

bool ImageFitter::CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values) {
    InitialValueCalculator* calculator =
        new InitialValueCalculator(_fit_data.data, _fit_data.width, _fit_data.n / _fit_data.width, _fit_data.offset_x, _fit_data.offset_y);
    bool success = calculator->CalculateInitialValues(initial_values);
    return success;
}

int ImageFitter::SolveSystem(CARTA::FittingSolverType solver) {
    gsl_multifit_nlinear_parameters fdf_params = gsl_multifit_nlinear_default_parameters();
    const gsl_multifit_nlinear_type* T = gsl_multifit_nlinear_trust;
    switch (solver) {
        case CARTA::FittingSolverType::Qr:
            fdf_params.solver = gsl_multifit_nlinear_solver_qr;
            break;
        case CARTA::FittingSolverType::Svd:
            fdf_params.solver = gsl_multifit_nlinear_solver_svd;
            break;
        case CARTA::FittingSolverType::Cholesky:
        default:
            fdf_params.solver = gsl_multifit_nlinear_solver_cholesky;
            break;
    }

    const double xtol = 1.0e-8;
    const double gtol = 1.0e-8;
    const double ftol = 1.0e-8;
    const size_t n = _fdf.n;
    const size_t p = _fdf.p;
    gsl_multifit_nlinear_workspace* work = gsl_multifit_nlinear_alloc(T, &fdf_params, n, p);
    gsl_vector* f = gsl_multifit_nlinear_residual(work);
    gsl_vector* y = gsl_multifit_nlinear_position(work);
    gsl_matrix* covar = gsl_matrix_alloc(p, p);

    gsl_vector* weights = gsl_vector_alloc(n);
    gsl_vector_set_all(weights, 1 / _image_std / _image_std);
    gsl_multifit_nlinear_winit(_fit_values, weights, &_fdf, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq0);

    GeneratorProgressCallback iteration_progress_callback = [&](size_t iter) {
        _progress_callback((iter + 1.0) / (_max_iter + 2.0)); // 2 for preparing fitting and generating results
    };
    int status = gsl_multifit_nlinear_driver(_max_iter, xtol, gtol, ftol, Callback, &iteration_progress_callback, &_fit_status.info, work);
    if (!_fit_data.stop_fitting) {
        iteration_progress_callback(_max_iter);

        gsl_blas_ddot(f, f, &_fit_status.chisq);
        gsl_multifit_nlinear_rcond(&_fit_status.rcond, work);
        gsl_vector_memcpy(_fit_values, y);

        CalculateErrors();

        size_t last_index = _fit_data.fit_values_indexes.size() - 1;
        int background_offset_index = _fit_data.fit_values_indexes[last_index];
        if (background_offset_index >= 0) {
            gsl_matrix* jac = gsl_multifit_nlinear_jac(work);
            gsl_multifit_nlinear_covar(jac, 0.0, covar);
            const double c = GSL_MAX_DBL(1, sqrt(_fit_status.chisq / (_fit_data.n_notnan - p)));
            gsl_vector_set(
                _fit_errors, background_offset_index, c * sqrt(gsl_matrix_get(covar, background_offset_index, background_offset_index)));
        }

        _fit_status.method = fmt::format("{}/{}", gsl_multifit_nlinear_name(work), gsl_multifit_nlinear_trs_name(work));
        _fit_status.num_iter = gsl_multifit_nlinear_niter(work);
        _fit_status.chisq0 *= _image_std * _image_std;
        _fit_status.chisq *= _image_std * _image_std;

        if (!status || (status == GSL_EMAXITER && _fit_status.num_iter == _max_iter)) {
            gsl_vector_scale(f, _image_std);
            CalculateImageData(f);
        }
    }

    gsl_multifit_nlinear_free(work);
    gsl_vector_free(weights);
    gsl_matrix_free(covar);
    return status;
}

void ImageFitter::CalculateErrors() {
    if (_unit == "Jy/beam" || _unit == "Jy/pixel") {
        _integrated_flux_values.resize(_num_components);
        _integrated_flux_errors.resize(_num_components);
    }

    for (size_t i = 0; i < _num_components; i++) {
        double center_x, center_y, amp, fwhm_x, fwhm_y, pa;
        std::tie(center_x, center_y, amp, fwhm_x, fwhm_y, pa) =
            GetGaussianParams(_fit_values, i * 6, _fit_data.fit_values_indexes, _fit_data.initial_values, 0, 0);
        double center_x_err, center_y_err, amp_err, fwhm_x_err, fwhm_y_err, pa_err;

        if (_beam_size > 0) {
            const double a = fwhm_x * fwhm_y / 4 / _beam_size / _beam_size * amp * amp / _image_std / _image_std;
            const double b = 1 + (_beam_size / fwhm_x) * (_beam_size / fwhm_x);
            const double c = 1 + (_beam_size / fwhm_y) * (_beam_size / fwhm_y);
            const double rho_square_1 = a * pow(b, 3.0 / 2.0) * pow(c, 3.0 / 2.0); // for amp
            const double rho_square_2 = a * pow(b, 5.0 / 2.0) * pow(c, 1.0 / 2.0); // for center x, fwhm x
            const double rho_square_3 = a * pow(b, 1.0 / 2.0) * pow(c, 5.0 / 2.0); // for center y, fwhm y, pa

            const double sq_center_major_err = fwhm_x * fwhm_x * SQ_FWHM_TO_SIGMA * 2.0 / rho_square_2;
            const double sq_center_minor_err = fwhm_y * fwhm_y * SQ_FWHM_TO_SIGMA * 2.0 / rho_square_3;
            center_x_err =
                sqrt(sq_center_major_err * pow(sin(pa * DEG_TO_RAD), 2.0) + sq_center_minor_err * pow(cos(pa * DEG_TO_RAD), 2.0));
            center_y_err =
                sqrt(sq_center_major_err * pow(cos(pa * DEG_TO_RAD), 2.0) + sq_center_minor_err * pow(sin(pa * DEG_TO_RAD), 2.0));
            amp_err = sqrt(amp * amp * 2.0 / rho_square_1);
            fwhm_x_err = sqrt(fwhm_x * fwhm_x * 2.0 / rho_square_2);
            fwhm_y_err = sqrt(fwhm_y * fwhm_y * 2.0 / rho_square_3);
            const double tmp = fwhm_x * fwhm_y / (fwhm_x * fwhm_x - fwhm_y * fwhm_y);
            pa_err = sqrt(4.0 * tmp * tmp / rho_square_3) * 180.0 / M_PI;

            if (_unit == "Jy/beam") {
                const double beam = M_PI * _beam_size * _beam_size / 4.0 / log(2.0);
                const double flux = 2 * M_PI * fwhm_x * fwhm_y * SQ_FWHM_TO_SIGMA * amp / beam;
                _integrated_flux_values[i] = flux;
                _integrated_flux_errors[i] =
                    sqrt(flux * flux *
                         (2.0 / rho_square_1 + (_beam_size * _beam_size / fwhm_x / fwhm_y) * (2.0 / rho_square_2 + 2.0 / rho_square_3)));
            }
        } else {
            const double rho_square = M_PI * fwhm_x * fwhm_y * SQ_FWHM_TO_SIGMA * amp * amp / _image_std / _image_std;

            const double sq_center_major_err = fwhm_x * fwhm_x * SQ_FWHM_TO_SIGMA * 2.0 / rho_square;
            const double sq_center_minor_err = fwhm_y * fwhm_y * SQ_FWHM_TO_SIGMA * 2.0 / rho_square;
            center_x_err =
                sqrt(sq_center_major_err * pow(sin(pa * DEG_TO_RAD), 2.0) + sq_center_minor_err * pow(cos(pa * DEG_TO_RAD), 2.0));
            center_y_err =
                sqrt(sq_center_major_err * pow(cos(pa * DEG_TO_RAD), 2.0) + sq_center_minor_err * pow(sin(pa * DEG_TO_RAD), 2.0));
            amp_err = sqrt(amp * amp * 2.0 / rho_square);
            fwhm_x_err = sqrt(fwhm_x * fwhm_x * 2.0 / rho_square);
            fwhm_y_err = sqrt(fwhm_y * fwhm_y * 2.0 / rho_square);
            const double tmp = fwhm_x * fwhm_y / (fwhm_x * fwhm_x - fwhm_y * fwhm_y);
            pa_err = sqrt(4.0 * tmp * tmp / rho_square) * 180.0 / M_PI;

            if (_unit == "Jy/pixel") {
                const double flux = 2 * M_PI * fwhm_x * fwhm_y * SQ_FWHM_TO_SIGMA * amp;
                _integrated_flux_values[i] = flux;
                _integrated_flux_errors[i] = sqrt(flux * flux * 2.0 / rho_square);
            }
        }

        auto setError = [&](int j, double value) {
            int fit_values_index = _fit_data.fit_values_indexes[i * 6 + j];
            if (fit_values_index >= 0) {
                gsl_vector_set(_fit_errors, fit_values_index, value);
            }
        };

        setError(0, center_x_err);
        setError(1, center_y_err);
        setError(2, amp_err);
        setError(3, fwhm_x_err);
        setError(4, fwhm_y_err);
        setError(5, pa_err);
    }
}

void ImageFitter::CalculateImageData(const gsl_vector* residual) {
    size_t size = residual->size;
    _model_data.resize(size);
    _residual_data.resize(size);
    for (size_t i = 0; i < size; i++) {
        if (_create_model_data) {
            _model_data[i] = _fit_data.data[i] - gsl_vector_get(residual, i);
        }
        if (_create_residual_data) {
            _residual_data[i] = isnan(_fit_data.data[i]) ? _fit_data.data[i] : gsl_vector_get(residual, i);
        }
    }
}

std::string ImageFitter::GetLog() {
    std::string info;
    switch (_fit_status.info) {
        case 1:
            info = "small step size";
            break;
        case 2:
            info = "small gradient";
            break;
        case 0:
        default:
            info = "exceeded max number of iterations";
            break;
    }

    std::string log = fmt::format("Gaussian fitting with {} component(s)\n", _num_components);
    log += fmt::format("summary from method '{}':\n", _fit_status.method);
    log += fmt::format("number of iterations = {}\n", _fit_status.num_iter);
    log += fmt::format("function evaluations = {}\n", _fdf.nevalf);
    log += fmt::format("Jacobian evaluations = {}\n", _fdf.nevaldf);
    log += fmt::format("reason for stopping  = {}\n", info);
    log += fmt::format("initial |f(x)|       = {:.12e}\n", sqrt(_fit_status.chisq0));
    log += fmt::format("final |f(x)|         = {:.12e}\n", sqrt(_fit_status.chisq));
    log += fmt::format("initial cost         = {:.12e}\n", _fit_status.chisq0);
    log += fmt::format("final cost           = {:.12e}\n", _fit_status.chisq);
    log += fmt::format("residual variance    = {:.12e}\n", _fit_status.chisq / (_fit_data.n_notnan - _fdf.p));
    log += fmt::format("final cond(J)        = {:.12e}\n", 1.0 / _fit_status.rcond);

    return log;
}

casa::SPIIF ImageFitter::GetImageData(casa::SPIIF image, const casacore::ImageRegion& image_region, std::vector<float> image_data) {
    casa::SPIIF sub_image(new casacore::SubImage<casacore::Float>(*image, image_region));
    casacore::CoordinateSystem csys = sub_image->coordinates();
    casacore::IPosition shape = sub_image->shape();
    casa::SPIIF output_image(new casacore::TempImage<casacore::Float>(casacore::TiledShape(shape), csys));
    output_image->setUnits(sub_image->units());
    output_image->setMiscInfo(sub_image->miscInfo());
    output_image->appendLog(sub_image->logger());

    auto image_info = sub_image->imageInfo();
    if (image_info.hasMultipleBeams()) {
        // Use first beam, per imageanalysis ImageCollapser
        auto beam = *(image_info.getBeamSet().getBeams().begin());
        image_info.removeRestoringBeam();
        image_info.setRestoringBeam(beam);
    }
    output_image->setImageInfo(image_info);

    casacore::Array<float> data_array(shape, image_data.data());
    output_image->put(data_array);
    output_image->flush();
    return output_image;
}

std::string ImageFitter::GetFilename(const std::string& filename, std::string suffix) {
    std::string moment_suffix;
    fs::path filepath;
    if (filename.rfind(".moment.") != std::string::npos) {
        std::string input_filename = filename.substr(0, filename.rfind(".moment."));
        filepath = fs::path(input_filename);
        moment_suffix = filename.substr(filename.rfind(".moment."));
    } else {
        filepath = fs::path(filename);
    }

    fs::path output_filename = filepath.stem();
    output_filename += "_" + suffix;
    output_filename += filepath.extension();
    return output_filename.string() + moment_suffix;
}

int ImageFitter::FuncF(const gsl_vector* fit_values, void* fit_data, gsl_vector* f) {
    struct FitData* d = (struct FitData*)fit_data;

    size_t last_index = d->fit_values_indexes.size() - 1;
    int background_offset_index = d->fit_values_indexes[last_index];
    double background_offset =
        background_offset_index < 0 ? d->initial_values[last_index] : gsl_vector_get(fit_values, background_offset_index);

    for (size_t k = 0; k < d->fit_values_indexes.size() - 1; k += 6) {
        // set residuals to zero to stop fitting procedure
        if (d->stop_fitting) {
            gsl_vector_set_zero(f);
            return GSL_SUCCESS;
        }

        double center_x, center_y, amp, fwhm_x, fwhm_y, pa;
        std::tie(center_x, center_y, amp, fwhm_x, fwhm_y, pa) = GetGaussianParams(fit_values, k, d->fit_values_indexes, d->initial_values);

        const double dbl_sq_std_x = 2 * fwhm_x * fwhm_x * SQ_FWHM_TO_SIGMA;
        const double dbl_sq_std_y = 2 * fwhm_y * fwhm_y * SQ_FWHM_TO_SIGMA;
        const double theta_radian = (pa - 90.0) * DEG_TO_RAD; // counterclockwise rotation
        const double a = cos(theta_radian) * cos(theta_radian) / dbl_sq_std_x + sin(theta_radian) * sin(theta_radian) / dbl_sq_std_y;
        const double dbl_b = 2 * (sin(2 * theta_radian) / (2 * dbl_sq_std_x) - sin(2 * theta_radian) / (2 * dbl_sq_std_y));
        const double c = sin(theta_radian) * sin(theta_radian) / dbl_sq_std_x + cos(theta_radian) * cos(theta_radian) / dbl_sq_std_y;

#pragma omp parallel for
        for (size_t i = 0; i < d->n; i++) {
            float data_i = d->data[i] - background_offset;
            if (!isnan(data_i)) {
                double dx = i % d->width - center_x;
                double dy = i / d->width - center_y;
                float data = amp * exp(-(a * dx * dx + dbl_b * dx * dy + c * dy * dy));
                if (k == 0) {
                    gsl_vector_set(f, i, data_i - data);
                } else {
                    gsl_vector_set(f, i, gsl_vector_get(f, i) - data);
                }
            } else {
                gsl_vector_set(f, i, 0);
            }
        }
    }

    return GSL_SUCCESS;
}

void ImageFitter::Callback(const size_t iter, void* params, const gsl_multifit_nlinear_workspace* w) {
    (*(GeneratorProgressCallback*)params)(iter);

    gsl_vector* f = gsl_multifit_nlinear_residual(w);
    gsl_vector* x = gsl_multifit_nlinear_position(w);
    double avratio = gsl_multifit_nlinear_avratio(w);
    double rcond;
    gsl_multifit_nlinear_rcond(&rcond, w);

    spdlog::debug("iter {}, |a|/|v| = {:.4f} cond(J) = {:8.4f}, |f(x)| = {:.4f}", iter, avratio, 1.0 / rcond, gsl_blas_dnrm2(f));
    std::string param_string = "params: ";
    for (int i = 0; i < x->size; ++i) {
        param_string += fmt::format("{:.12f} ", gsl_vector_get(x, i));
    }
    spdlog::debug(param_string);
}

void ImageFitter::ErrorHandler(const char* reason, const char* file, int line, int gsl_errno) {
    spdlog::error("gsl error: {} line{}: {}", file, line, reason);
}

std::tuple<double, double, double, double, double, double> ImageFitter::GetGaussianParams(const gsl_vector* value_vector, size_t index,
    std::vector<int>& fit_values_indexes, std::vector<double>& initial_values, size_t offset_x, size_t offset_y) {
    auto getParam = [&](int i) {
        int fit_values_index = fit_values_indexes[index + i];
        return fit_values_index < 0 ? initial_values[index + i] : gsl_vector_get(value_vector, fit_values_index);
    };
    double center_x = getParam(0) + offset_x;
    double center_y = getParam(1) + offset_y;
    double amp = getParam(2);
    double fwhm_x = getParam(3);
    double fwhm_y = getParam(4);
    double pa = getParam(5);
    std::tuple<double, double, double, double, double, double> params = {center_x, center_y, amp, fwhm_x, fwhm_y, pa};
    return params;
}

CARTA::GaussianComponent ImageFitter::GetGaussianComponent(std::tuple<double, double, double, double, double, double> params) {
    auto [center_x, center_y, amp, fwhm_x, fwhm_y, pa] = params;
    auto center = Message::DoublePoint(center_x, center_y);
    auto fwhm = Message::DoublePoint(fwhm_x, fwhm_y);
    auto component = Message::GaussianComponent(center, amp, fwhm, pa);
    return component;
}

double ImageFitter::GetMedianAbsDeviation(const size_t n, double x[]) {
    double* work = (double*)malloc(n * sizeof(double));
    double mad;

    mad = gsl_stats_mad(x, 1, n, work);

    free(work);
    return mad;
}
