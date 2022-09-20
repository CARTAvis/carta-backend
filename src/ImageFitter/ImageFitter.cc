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
    bool create_model_image, bool create_residual_image, CARTA::FittingResponse& fitting_response, GeneratorProgressCallback progress_callback, size_t offset_x, size_t offset_y) {
    bool success = false;
    _fit_data.stop_fitting = false;
    _model_data.clear();
    _residual_data.clear();

    _fit_data.width = width;
    _fit_data.n = width * height;
    _fit_data.data = image;
    _fit_data.offset_x = offset_x;
    _fit_data.offset_y = offset_y;
    _fdf.n = _fit_data.n;
    _create_model_data = create_model_image;
    _create_residual_data = create_residual_image;
    _progress_callback = progress_callback;

    CalculateNanNum();
    SetInitialValues(initial_values);

    // avoid SolveSystem crashes with insufficient data points
    if (_fit_data.n_notnan < _num_components * 6) {
        fitting_response.set_message("insufficient data points");
        fitting_response.set_success(success);

        gsl_vector_free(_fit_values);
        gsl_vector_free(_fit_errors);
        return false;
    }

    spdlog::info("Fitting image ({} data points) with {} Gaussian component(s).", _fit_data.n_notnan, _num_components);
    int status = SolveSystem();

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
                fitting_response.add_result_values();
                *fitting_response.mutable_result_values(i) = GetGaussianComponent(_fit_values, i);
                fitting_response.add_result_errors();
                *fitting_response.mutable_result_errors(i) = GetGaussianComponent(_fit_errors, i);
            }
            fitting_response.set_log(GetLog());
        }
    }
    fitting_response.set_success(success);

    gsl_vector_free(_fit_values);
    gsl_vector_free(_fit_errors);
    return success;
}

bool ImageFitter::GetGeneratedImages(casa::SPIIF image, const casacore::ImageRegion& image_region,
    int file_id, const std::string& filename, GeneratedImage& model_image, GeneratedImage& residual_image) {
    // Todo: find another better way to assign the temp file Id
    int id = (file_id + 1) * ID_MULTIPLIER - 1;

    if (_create_model_data) {
        model_image = GeneratedImage(id, GetFilename(filename, "model"), GetImageData(image, image_region, _model_data));
    }
    if (_create_residual_data) {
        residual_image = GeneratedImage(id - 1, GetFilename(filename, "residual"), GetImageData(image, image_region, _residual_data));
    }
    return true;
}

void ImageFitter::StopFitting() {
    _fit_data.stop_fitting = true;
}

void ImageFitter::CalculateNanNum() {
    _fit_data.n_notnan = _fit_data.n;
    for (size_t i = 0; i < _fit_data.n; i++) {
        if (isnan(_fit_data.data[i])) {
            _fit_data.n_notnan--;
        }
    }
}

void ImageFitter::SetInitialValues(const std::vector<CARTA::GaussianComponent>& initial_values) {
    _num_components = initial_values.size();

    size_t p = _num_components * 6;
    _fit_values = gsl_vector_alloc(p);
    _fit_errors = gsl_vector_alloc(p);
    for (size_t i = 0; i < _num_components; i++) {
        CARTA::GaussianComponent component(initial_values[i]);
        gsl_vector_set(_fit_values, i * 6, component.center().x() - _fit_data.offset_x);
        gsl_vector_set(_fit_values, i * 6 + 1, component.center().y() - _fit_data.offset_y);
        gsl_vector_set(_fit_values, i * 6 + 2, component.amp());
        gsl_vector_set(_fit_values, i * 6 + 3, component.fwhm().x());
        gsl_vector_set(_fit_values, i * 6 + 4, component.fwhm().y());
        gsl_vector_set(_fit_values, i * 6 + 5, component.pa());
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
    const size_t n = _fdf.n;
    const size_t p = _fdf.p;
    gsl_multifit_nlinear_workspace* work = gsl_multifit_nlinear_alloc(T, &fdf_params, n, p);
    gsl_vector* f = gsl_multifit_nlinear_residual(work);
    gsl_vector* y = gsl_multifit_nlinear_position(work);
    gsl_matrix* covar = gsl_matrix_alloc(p, p);

    gsl_multifit_nlinear_init(_fit_values, &_fdf, work);
    gsl_blas_ddot(f, f, &_fit_status.chisq0);

    GeneratorProgressCallback iteration_progress_callback = [&](size_t iter) {
        _progress_callback((iter + 1.0) / (_max_iter + 2.0)); // 2 for preparing fitting and generating results
    };
    int status =
        gsl_multifit_nlinear_driver(_max_iter, xtol, gtol, ftol, Callback, &iteration_progress_callback, &_fit_status.info, work);
    if (!_fit_data.stop_fitting) {
        iteration_progress_callback(_max_iter);
    }
    
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

    CalculateImageData(f);

    gsl_multifit_nlinear_free(work);
    gsl_matrix_free(covar);

    for (size_t i = 0; i < _num_components; i++) {
        gsl_vector_set(_fit_values, i * 6, gsl_vector_get(_fit_values, i * 6) + _fit_data.offset_x);
        gsl_vector_set(_fit_values, i * 6 + 1, gsl_vector_get(_fit_values, i * 6 + 1) + _fit_data.offset_y);
    }

    return status;
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
    fs::path filepath(filename);
    fs::path output_filename = filepath.stem();
    output_filename += "_" + suffix;
    output_filename += filepath.extension();
    return output_filename.string();
}

int ImageFitter::FuncF(const gsl_vector* fit_values, void* fit_data, gsl_vector* f) {
    struct FitData* d = (struct FitData*)fit_data;

    for (size_t k = 0; k < fit_values->size; k += 6) {
        // set residuals to zero to stop fitting procedure
        if (d->stop_fitting) {
            gsl_vector_set_zero(f);
            return GSL_SUCCESS;
        }

        const double center_x = gsl_vector_get(fit_values, k);
        const double center_y = gsl_vector_get(fit_values, k + 1);
        const double amp = gsl_vector_get(fit_values, k + 2);
        const double fwhm_x = gsl_vector_get(fit_values, k + 3);
        const double fwhm_y = gsl_vector_get(fit_values, k + 4);
        const double pa = gsl_vector_get(fit_values, k + 5);

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
    (*(GeneratorProgressCallback*)params)(iter);

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

CARTA::GaussianComponent ImageFitter::GetGaussianComponent(gsl_vector* value_vector, size_t index) {
    auto center = Message::DoublePoint(gsl_vector_get(value_vector, index * 6), gsl_vector_get(value_vector, index * 6 + 1));
    double amp = gsl_vector_get(value_vector, index * 6 + 2);
    auto fwhm = Message::DoublePoint(gsl_vector_get(value_vector, index * 6 + 3), gsl_vector_get(value_vector, index * 6 + 4));
    double pa = gsl_vector_get(value_vector, index * 6 + 5);
    auto component = Message::GaussianComponent(center, amp, fwhm, pa);
    return component;
}
