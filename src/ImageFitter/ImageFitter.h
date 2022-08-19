/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    size_t n;
    size_t n_notnan; // number of pixels excluding nan pixels
    size_t offset_x;
    size_t offset_y;
    std::vector<int> fit_values_indexes;
    std::vector<float> initial_values;
};

struct FitStatus {
    std::string method;
    size_t num_iter;
    int info;
    double chisq0, chisq, rcond;
};

class ImageFitter {
public:
    ImageFitter();
    bool FitImage(size_t width, size_t height, float* image, const std::vector<CARTA::GaussianComponent>& initial_values,
        const std::vector<bool>& fixed_params, CARTA::FittingResponse& fitting_response, size_t offset_x = 0, size_t offset_y = 0);

private:
    FitData _fit_data;
    size_t _num_components;
    gsl_vector* _fit_values;
    gsl_vector* _fit_errors;
    gsl_multifit_nlinear_fdf _fdf;
    FitStatus _fit_status;
    const size_t _max_iter = 200;

    void CalculateNanNum();
    void SetInitialValues(const std::vector<CARTA::GaussianComponent>& initial_values, const std::vector<bool>& fixed_params);
    int SolveSystem();
    void SetResults();
    std::string GetLog();

    static int FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f);
    static void Callback(const size_t iter, void* params, const gsl_multifit_nlinear_workspace* w);
    static void ErrorHandler(const char* reason, const char* file, int line, int gsl_errno);
    static std::tuple<double, double, double, double, double, double> GetGaussianParams(const gsl_vector* value_vector, size_t index,
        std::vector<int>& fit_values_indexes, std::vector<float>& initial_values, size_t offset_x = 0, size_t offset_y = 0);
    static CARTA::GaussianComponent GetGaussianComponent(std::tuple<double, double, double, double, double, double> params);
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
