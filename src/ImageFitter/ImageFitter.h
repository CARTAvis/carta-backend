/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
#define CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_

#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_vector.h>
#include <string>
#include <vector>

#include <carta-protobuf/fitting_request.pb.h>

#include "Logger/Logger.h"

namespace carta {

struct FitData {
    float* data;
    size_t width;
    size_t n; // number of pixels excluding nan pixels
};

struct FitStatus {
    std::string method;
    size_t num_iter;
    int info;
    double chisq0, chisq, rcond;
};

class ImageFitter {
public:
    ImageFitter(size_t width, size_t height, std::string unit);
    bool FitImage(float* image, const std::vector<CARTA::GaussianComponent>& initial_values);
    std::string GetMessage() {
        return _message;
    };
    std::string GetResults() {
        return _results;
    };
    std::string GetLog() {
        return _log;
    };

private:
    FitData _fit_data;
    std::string _image_unit;
    size_t _num_components;
    gsl_vector* _fit_params;
    gsl_vector* _fit_errors;
    gsl_multifit_nlinear_fdf _fdf;
    FitStatus _fit_status;
    std::string _message;
    std::string _results;
    std::string _log;
    const size_t _max_iter = 200;

    void CalculateNanNum();
    void SetInitialValues(const std::vector<CARTA::GaussianComponent>& initial_values);
    int SolveSystem();
    void SetResults();
    void SetLog();

    static int FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f);
    static void Callback(const size_t iter, void* params, const gsl_multifit_nlinear_workspace* w);
    static void ErrorHandler(const char* reason, const char* file, int line, int gsl_errno);
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
