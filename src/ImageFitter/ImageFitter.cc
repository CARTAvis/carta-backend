/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define SQ_FWHM_TO_SIGMA 1 / 8 / log(2)
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

bool ImageFitter::FitImage(size_t width, size_t height, float* image, const std::vector<CARTA::GaussianComponent>& initial_values,
    const std::vector<bool>& fixed_params, CARTA::FittingResponse& fitting_response, size_t offset_x, size_t offset_y) {
    bool success = false;

    _fit_data.width = width;
    _fit_data.n = width * height;
    _fit_data.data = image;
    _fit_data.offset_x = offset_x;
    _fit_data.offset_y = offset_y;
    _fdf.n = _fit_data.n;

    CalculateNanNum();
    SetInitialValues(initial_values, fixed_params);

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
    int status = SolveSystem();

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
        fitting_response.set_log(GetLog());
    }
    fitting_response.set_success(success);

    gsl_vector_free(_fit_values);
    gsl_vector_free(_fit_errors);
    return success;
}

void ImageFitter::CalculateNanNum() {
    _fit_data.n_notnan = _fit_data.n;
    for (size_t i = 0; i < _fit_data.n; i++) {
        if (isnan(_fit_data.data[i])) {
            _fit_data.n_notnan--;
        }
    }
}

void ImageFitter::SetInitialValues(const std::vector<CARTA::GaussianComponent>& initial_values, const std::vector<bool>& fixed_params) {
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

    size_t p = std::count(fixed_params.begin(), fixed_params.end(), false);
    _fit_values = gsl_vector_alloc(p);
    _fit_errors = gsl_vector_alloc(p);
    size_t iter = 0;
    _fit_data.fit_values_indexes.clear();
    for (size_t i = 0; i < fixed_params.size(); i++) {
        if (!fixed_params[i]) {
            _fit_data.fit_values_indexes.push_back(iter);
            gsl_vector_set(_fit_values, iter, _fit_data.initial_values[i]);
            iter++;
        } else {
            _fit_data.fit_values_indexes.push_back(-1);
        }
    }
    _fdf.p = p;
}

int ImageFitter::SolveSystem() {
    gsl_multifit_nlinear_parameters fdf_params = gsl_multifit_nlinear_default_parameters();
    const gsl_multifit_nlinear_type* T = gsl_multifit_nlinear_trust;
    fdf_params.solver = gsl_multifit_nlinear_solver_cholesky;
    const double xtol = 1.0e-8;
    const double gtol = 1.0e-8;
    const double ftol = 1.0e-8;
    const bool print_iter = false;
    const size_t n = _fdf.n;
    const size_t p = _fdf.p;
    gsl_multifit_nlinear_workspace* work = gsl_multifit_nlinear_alloc(T, &fdf_params, n, p);
    gsl_vector* f = gsl_multifit_nlinear_residual(work);
    gsl_vector* y = gsl_multifit_nlinear_position(work);
    gsl_matrix* covar = gsl_matrix_alloc(p, p);

    gsl_multifit_nlinear_init(_fit_values, &_fdf, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq0);
    int status =
        gsl_multifit_nlinear_driver(_max_iter, xtol, gtol, ftol, print_iter ? Callback : nullptr, nullptr, &_fit_status.info, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq);
    gsl_multifit_nlinear_rcond(&_fit_status.rcond, work);
    gsl_vector_memcpy(_fit_values, y);

    gsl_matrix* jac = gsl_multifit_nlinear_jac(work);
    gsl_multifit_nlinear_covar(jac, 0.0, covar);
    const double c = sqrt(_fit_status.chisq / (_fit_data.n_notnan - p));
    for (size_t i = 0; i < p; i++) {
        gsl_vector_set(_fit_errors, i, c * sqrt(gsl_matrix_get(covar, i, i)));
    }

    _fit_status.method = fmt::format("{}/{}", gsl_multifit_nlinear_name(work), gsl_multifit_nlinear_trs_name(work));
    _fit_status.num_iter = gsl_multifit_nlinear_niter(work);

    gsl_multifit_nlinear_free(work);
    gsl_matrix_free(covar);

    return status;
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

int ImageFitter::FuncF(const gsl_vector* fit_values, void* fit_data, gsl_vector* f) {
    struct FitData* d = (struct FitData*)fit_data;

    for (size_t k = 0; k < d->fit_values_indexes.size(); k += 6) {
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
            float data_i = d->data[i];
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
