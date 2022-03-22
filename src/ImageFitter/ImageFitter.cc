/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define FWHM_TO_SIGMA 0.5 / sqrt(2 * log(2))
#define DEG_TO_RAD M_PI / 180.0

#include "ImageFitter.h"

#include <omp.h>

using namespace carta;

ImageFitter::ImageFitter(float* image, size_t width, size_t height, std::string unit) {
    _fit_data.data = image;
    _fit_data.width = width;
    _fit_data.n = width * height;
    _image_unit = unit;

    _fdf.f = FuncF;
    _fdf.df = nullptr; // internally computed using finite difference approximations of f when set to NULL
    _fdf.fvv = nullptr;
    _fdf.params = &_fit_data;
    _fdf.n = _fit_data.n;

    // avoid GSL default error handler calling abort()
    gsl_set_error_handler(&ErrorHandler);
}

bool ImageFitter::FitImage(const std::vector<CARTA::GaussianComponent>& initial_values) {
    bool success = false;
    _message = "";
    _results = "";
    _log = "";
    CalculateNanNum();
    SetInitialValues(initial_values);

    spdlog::info("Fitting image ({} data points) with {} Gaussian component(s).", _fit_data.n, _num_components);
    int status = SolveSystem();

    if (status == GSL_EMAXITER && _fit_status.num_iter < _max_iter) {
        _message = "fit did not converge";
    } else if (status) {
        _message = gsl_strerror(status);
    }

    if (!status || (status == GSL_EMAXITER && _fit_status.num_iter == _max_iter)) {
        success = true;
        spdlog::info("Writing fitting results and log.");
        SetResults();
        SetLog();
    }

    gsl_vector_free(_fit_params);
    gsl_vector_free(_fit_errors);
    return success;
}

void ImageFitter::CalculateNanNum() {
    _fit_data.n = _fdf.n;
    for (size_t i = 0; i < _fit_data.n; i++) {
        if (isnan(_fit_data.data[i])) {
            _fit_data.n--;
        }
    }
}

void ImageFitter::SetInitialValues(const std::vector<CARTA::GaussianComponent>& initial_values) {
    _num_components = initial_values.size();

    size_t p = _num_components * 6;
    _fit_params = gsl_vector_alloc(p);
    _fit_errors = gsl_vector_alloc(p);
    for (size_t i = 0; i < _num_components; i++) {
        CARTA::GaussianComponent component(initial_values[i]);
        gsl_vector_set(_fit_params, i * 6, component.center_x());
        gsl_vector_set(_fit_params, i * 6 + 1, component.center_y());
        gsl_vector_set(_fit_params, i * 6 + 2, component.amp());
        gsl_vector_set(_fit_params, i * 6 + 3, component.fwhm_x());
        gsl_vector_set(_fit_params, i * 6 + 4, component.fwhm_y());
        gsl_vector_set(_fit_params, i * 6 + 5, component.pa());
    }
    _fdf.p = p;
}

int ImageFitter::SolveSystem() {
    gsl_multifit_nlinear_parameters fdf_params = gsl_multifit_nlinear_default_parameters();
    const gsl_multifit_nlinear_type* T = gsl_multifit_nlinear_trust;
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

    gsl_multifit_nlinear_init(_fit_params, &_fdf, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq0);
    int status =
        gsl_multifit_nlinear_driver(_max_iter, xtol, gtol, ftol, print_iter ? Callback : nullptr, nullptr, &_fit_status.info, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq);
    gsl_multifit_nlinear_rcond(&_fit_status.rcond, work);
    gsl_vector_memcpy(_fit_params, y);

    gsl_matrix* jac = gsl_multifit_nlinear_jac(work);
    gsl_multifit_nlinear_covar(jac, 0.0, covar);
    const double c = sqrt(_fit_status.chisq / (_fit_data.n - p));
    for (size_t i = 0; i < p; i++) {
        gsl_vector_set(_fit_errors, i, c * sqrt(gsl_matrix_get(covar, i, i)));
    }

    _fit_status.method = fmt::format("{}/{}", gsl_multifit_nlinear_name(work), gsl_multifit_nlinear_trs_name(work));
    _fit_status.num_iter = gsl_multifit_nlinear_niter(work);

    gsl_multifit_nlinear_free(work);
    gsl_matrix_free(covar);
    return status;
}

void ImageFitter::SetResults() {
    _results = "";
    for (size_t i = 0; i < _num_components; i++) {
        _results += fmt::format("Component #{}:\n", i + 1);
        _results +=
            fmt::format("Center X  = {:6f} +/- {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6), gsl_vector_get(_fit_errors, i * 6));
        _results += fmt::format(
            "Center Y  = {:6f} +/- {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 1), gsl_vector_get(_fit_errors, i * 6 + 1));
        _results += fmt::format("Amplitude = {:6f} +/- {:6f}{}\n", gsl_vector_get(_fit_params, i * 6 + 2),
            gsl_vector_get(_fit_errors, i * 6 + 2), _image_unit.size() > 0 ? fmt::format(" ({})", _image_unit) : "");
        _results += fmt::format(
            "FWHM X    = {:6f} +/- {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 3), gsl_vector_get(_fit_errors, i * 6 + 3));
        _results += fmt::format(
            "FWHM Y    = {:6f} +/- {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 4), gsl_vector_get(_fit_errors, i * 6 + 4));
        _results += fmt::format(
            "P.A.      = {:6f} +/- {:6f} (deg)\n", gsl_vector_get(_fit_params, i * 6 + 5), gsl_vector_get(_fit_errors, i * 6 + 5));
        _results += "\n";
    }
}

void ImageFitter::SetLog() {
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

    _log = "";
    _log += fmt::format("Gaussian fitting with {} component(s)\n", _num_components);
    _log += fmt::format("summary from method '{}':\n", _fit_status.method);
    _log += fmt::format("number of iterations = {}\n", _fit_status.num_iter);
    _log += fmt::format("function evaluations = {}\n", _fdf.nevalf);
    _log += fmt::format("Jacobian evaluations = {}\n", _fdf.nevaldf);
    _log += fmt::format("reason for stopping  = {}\n", info);
    _log += fmt::format("initial |f(x)|       = {:.12e}\n", sqrt(_fit_status.chisq0));
    _log += fmt::format("final |f(x)|         = {:.12e}\n", sqrt(_fit_status.chisq));
    _log += fmt::format("initial cost         = {:.12e}\n", _fit_status.chisq0);
    _log += fmt::format("final cost           = {:.12e}\n", _fit_status.chisq);
    _log += fmt::format("final cond(J)        = {:.12e}\n", 1.0 / _fit_status.rcond);
}

double ImageFitter::Gaussian(const double center_x, const double center_y, const double amp, const double fwhm_x, const double fwhm_y,
    const double theta, const double x, const double y) {
    const double sq_std_x = pow(fwhm_x * FWHM_TO_SIGMA, 2);
    const double sq_std_y = pow(fwhm_y * FWHM_TO_SIGMA, 2);
    const double theta_radian = theta * DEG_TO_RAD; // counterclockwise rotation
    const double a = pow(cos(theta_radian), 2) / (2 * sq_std_x) + pow(sin(theta_radian), 2) / (2 * sq_std_y);
    const double b = sin(2 * theta_radian) / (4 * sq_std_x) - sin(2 * theta_radian) / (4 * sq_std_y);
    const double c = pow(sin(theta_radian), 2) / (2 * sq_std_x) + pow(cos(theta_radian), 2) / (2 * sq_std_y);
    return amp * exp(-(a * pow((x - center_x), 2) + 2 * b * (x - center_x) * (y - center_y) + c * pow((y - center_y), 2)));
}

int ImageFitter::FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f) {
    struct FitData* d = (struct FitData*)fit_data;

#pragma omp parallel for
    for (size_t i = 0; i < d->n; i++) {
        float data_i = d->data[i];
        if (!isnan(data_i)) {
            int x = i % d->width;
            int y = i / d->width;
            float data = 0;

            for (size_t k = 0; k < fit_params->size; k += 6) {
                const double center_x = gsl_vector_get(fit_params, k);
                const double center_y = gsl_vector_get(fit_params, k + 1);
                const double amp = gsl_vector_get(fit_params, k + 2);
                const double fwhm_x = gsl_vector_get(fit_params, k + 3);
                const double fwhm_y = gsl_vector_get(fit_params, k + 4);
                const double pa = gsl_vector_get(fit_params, k + 5);
                data += Gaussian(center_x, center_y, amp, fwhm_x, fwhm_y, pa, x, y);
            }
            gsl_vector_set(f, i, data_i - data);
        } else {
            gsl_vector_set(f, i, 0);
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
    for (int k = 0; k < x->size / 6; ++k) {
        spdlog::debug("component {}: ({:.12f}, {:.12f}, {:.12f}, {:.12f}, {:.12f}, {:.12f})", k + 1, gsl_vector_get(x, k * 6),
            gsl_vector_get(x, k * 6 + 1), gsl_vector_get(x, k * 6 + 2), gsl_vector_get(x, k * 6 + 3), gsl_vector_get(x, k * 6 + 4),
            gsl_vector_get(x, k * 6 + 5));
    }
}

void ImageFitter::ErrorHandler(const char* reason, const char* file, int line, int gsl_errno) {
    spdlog::error("gsl error: {} line{}: {}", file, line, reason);
}
