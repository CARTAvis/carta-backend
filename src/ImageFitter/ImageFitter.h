/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
#define CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_

#include <string>
#include <vector>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_vector.h>

#include <carta-protobuf/fitting_request.pb.h>

#include "Logger/Logger.h"

namespace carta {

struct FitData {
    std::vector<float> x;
    std::vector<float> y;
    float* data;
    size_t n;
};

class ImageFitter {
    public:
        ImageFitter(float* image, size_t width, size_t height, std::string unit);
        bool FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response);

    private:
        FitData _fit_data;
        std::string _image_unit;
        size_t _num_components;
        gsl_vector* _fit_params;
        gsl_multifit_nlinear_fdf _fdf;
        bool _print_iter = false;
        std::string _method;
        size_t _num_iter;
        int _info;
        double _chisq0, _chisq, _rcond;
        std::string _results;
        std::string _log;

        void SetFitData(float* image, size_t width, size_t height);
        void SetInitialValues(const CARTA::FittingRequest& fitting_request);
        int SolveSystem();
        void SetResults();
        void SetLog();

        static double Gaussian(const double center_x, const double center_y, const double amp, const double fwhm_x, const double fwhm_y, const double theta,
                               const double x, const double y);
        static int FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f);
        static void Callback(const size_t iter, void *params, const gsl_multifit_nlinear_workspace *w);
        
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_