/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageFitter.h"

using namespace carta;

ImageFitter::ImageFitter(float* image, size_t width, size_t height, std::string unit) {
    SetFitData(image, width, height);
    _image_unit = unit;

    _fdf.f = FuncF;
    _fdf.df = NULL; // internally computed using finite difference approximations of f when set to NULL
    _fdf.fvv = NULL;
    _fdf.n = _fit_data.n;
    _fdf.params = &_fit_data;
}

bool ImageFitter::FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response) {
    SetInitialValues(fitting_request);
    int status = SolveSystem();
    if (!status) {
        spdlog::info("Writing fitting results and log.");
        SetResults();
        SetLog();
    }
    gsl_vector_free(_fit_params);

    if (status) {
        fitting_response.set_message(fmt::format("Image fitting failed: {}.", gsl_strerror(status)));
        return false;
    } else {
        fitting_response.set_success(true);
        fitting_response.set_results(_results);
        fitting_response.set_log(_log);
        return true;
    }
}

void ImageFitter::SetFitData(float* image, size_t width, size_t height) {
    size_t n = width * height;
    _fit_data.n = n;
    _fit_data.data = image;

    _fit_data.x.resize(n);
    _fit_data.y.resize(n);
    size_t index = 0;
    for (size_t i = 0; i < height; i++) {
        for (size_t j = 0; j < width; j++) {
            _fit_data.x[index] = j;
            _fit_data.y[index] = i;
            index++;
        }
    }
}

void ImageFitter::SetInitialValues(const CARTA::FittingRequest& fitting_request) {
    _num_components = fitting_request.initial_values_size();
    spdlog::info("Fitting image with {} Gaussian component(s).", _num_components);

    size_t p = _num_components * 6;
    _fit_params = gsl_vector_alloc(p);
    for (size_t i = 0; i < _num_components; i++) {
        CARTA::GaussianComponent component(fitting_request.initial_values()[i]);
        gsl_vector_set(_fit_params, i * 6 + 0, component.center_x());
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
    const size_t max_iter = 200;
    const double xtol = 1.0e-8;
    const double gtol = 1.0e-8;
    const double ftol = 1.0e-8;
    const size_t n = _fdf.n;
    const size_t p = _fdf.p;
    gsl_multifit_nlinear_workspace* work = gsl_multifit_nlinear_alloc(T, &fdf_params, n, p);
    gsl_vector* f = gsl_multifit_nlinear_residual(work);
    gsl_vector* y = gsl_multifit_nlinear_position(work);

    gsl_multifit_nlinear_init(_fit_params, &_fdf, work);
    gsl_blas_ddot(f, f, &_chisq0);
    int status = gsl_multifit_nlinear_driver(max_iter, xtol, gtol, ftol, _print_iter ? Callback : NULL, NULL, &_info, work);
    gsl_blas_ddot(f, f, &_chisq);
    gsl_multifit_nlinear_rcond(&_rcond, work);
    gsl_vector_memcpy(_fit_params, y);

    _method = fmt::format("{}/{}", gsl_multifit_nlinear_name(work), gsl_multifit_nlinear_trs_name(work));
    _num_iter = gsl_multifit_nlinear_niter(work);

    gsl_multifit_nlinear_free(work);
    return status;
}

void ImageFitter::SetResults() {
    _results = "";
    for (size_t i = 0; i < _num_components; i++) {
        _results += fmt::format("Component #{}:\n", i + 1);
        _results += fmt::format("Center X  = {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 0));
        _results += fmt::format("Center Y  = {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 1));
        _results += fmt::format("Amplitude = {:6f} ({})\n", gsl_vector_get(_fit_params, i * 6 + 2), _image_unit);
        _results += fmt::format("FWHM X    = {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 3));
        _results += fmt::format("FWHM Y    = {:6f} (px)\n", gsl_vector_get(_fit_params, i * 6 + 4));
        _results += fmt::format("P.A.      = {:6f} (deg)\n", gsl_vector_get(_fit_params, i * 6 + 5));
        _results += "\n";
    }
}

void ImageFitter::SetLog() {
    _log = "";
    _log += fmt::format("Gaussian fitting with {} component(s)\n", _num_components);
    _log += fmt::format("summary from method '{}':\n", _method);
    _log += fmt::format("number of iterations = {}\n", _num_iter);
    _log += fmt::format("function evaluations = {}\n", _fdf.nevalf);
    _log += fmt::format("Jacobian evaluations = {}\n", _fdf.nevaldf);
    _log += fmt::format("reason for stopping  = {}\n", (_info == 1) ? "small step size" : "small gradient");
    _log += fmt::format("initial |f(x)|       = {:.12e}\n", sqrt(_chisq0));
    _log += fmt::format("final |f(x)|         = {:.12e}\n", sqrt(_chisq));
    _log += fmt::format("initial cost         = {:.12e}\n", _chisq0);
    _log += fmt::format("final cost           = {:.12e}\n", _chisq);
    _log += fmt::format("final cond(J)        = {:.12e}\n", 1.0 / _rcond);
}

double ImageFitter::Gaussian(const double center_x, const double center_y, const double amp, const double fwhm_x, const double fwhm_y,
    const double theta, const double x, const double y) {
    const double sq_std_x = pow(fwhm_x, 2) / 8 / log(2);
    const double sq_std_y = pow(fwhm_y, 2) / 8 / log(2);
    const double theta_radian = theta * M_PI / 180.0;
    const double a = pow(cos(theta_radian), 2) / (2 * pow(sq_std_x, 2)) + pow(sin(theta_radian), 2) / (2 * pow(sq_std_y, 2));
    const double b = -sin(2 * theta_radian) / (4 * pow(sq_std_x, 2)) + sin(2 * theta_radian) / (4 * pow(sq_std_y, 2));
    const double c = pow(sin(theta_radian), 2) / (2 * pow(sq_std_x, 2)) + pow(cos(theta_radian), 2) / (2 * pow(sq_std_y, 2));
    return amp * exp(-(a * pow((x - center_x), 2) + 2 * b * (x - center_x) * (y - center_y) + c * pow((y - center_y), 2)));
}

int ImageFitter::FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f) {
    struct FitData* d = (struct FitData*)fit_data;

    for (size_t i = 0; i < d->n; i++) {
        float x = d->x[i];
        float y = d->y[i];
        float data_i = d->data[i];
        float data = 0;

        for (size_t k = 0; k < fit_params->size / 6; k++) {
            const double center_x = gsl_vector_get(fit_params, k * 6 + 0);
            const double center_y = gsl_vector_get(fit_params, k * 6 + 1);
            const double amp = gsl_vector_get(fit_params, k * 6 + 2);
            const double fwhm_x = gsl_vector_get(fit_params, k * 6 + 3);
            const double fwhm_y = gsl_vector_get(fit_params, k * 6 + 4);
            const double pa = gsl_vector_get(fit_params, k * 6 + 5);
            data += Gaussian(center_x, center_y, amp, fwhm_x, fwhm_y, pa, x, y);
        }

        gsl_vector_set(f, i, data_i - data);
    }

    return GSL_SUCCESS;
}

void ImageFitter::Callback(const size_t iter, void* params, const gsl_multifit_nlinear_workspace* w) {
    gsl_vector* f = gsl_multifit_nlinear_residual(w);
    gsl_vector* x = gsl_multifit_nlinear_position(w);
    double avratio = gsl_multifit_nlinear_avratio(w);
    double rcond;

    (void)params; /* not used */

    /* compute reciprocal condition number of J(x) */
    gsl_multifit_nlinear_rcond(&rcond, w);

    spdlog::debug("iter {}, |a|/|v| = {:.4f} cond(J) = {:8.4f}, |f(x)| = {:.4f}", iter, avratio, 1.0 / rcond, gsl_blas_dnrm2(f));
    for (int k = 0; k < x->size / 6; ++k) {
        spdlog::debug("component {}: ({:.12f}, {:.12f}, {:.12f}, {:.12f}, {:.12f}, {:.12f})", k + 1, gsl_vector_get(x, k * 6 + 0),
            gsl_vector_get(x, k * 6 + 1), gsl_vector_get(x, k * 6 + 2), gsl_vector_get(x, k * 6 + 3), gsl_vector_get(x, k * 6 + 4),
            gsl_vector_get(x, k * 6 + 5));
    }
}
