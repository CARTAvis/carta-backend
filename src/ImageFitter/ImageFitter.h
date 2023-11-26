/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
#define CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_

#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_vector.h>
#include <string>
#include <vector>

#include <carta-protobuf/fitting_request.pb.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/images/Images/TempImage.h>
#include <imageanalysis/ImageTypedefs.h>

#include "ImageGenerators/ImageGenerator.h"
#include "Logger/Logger.h"

namespace carta {

/** @brief Data structure for storing fitting-related data. */
struct FitData {
    /** @brief Pointer to the image data. */
    float* data;
    /** @brief The width of the image. */
    size_t width;
    /** @brief Number of pixels. */
    size_t n;
    /** @brief Number of pixels excluding nan pixels. */
    size_t n_notnan;
    /** @brief X-axis offset from the fitting region to the entire image. */
    size_t offset_x;
    /** @brief Y-axis offset from the fitting region to the entire image. */
    size_t offset_y;
    /** @brief Indexes of the Gaussian parameters in the fittig parameters. */
    std::vector<int> fit_values_indexes;
    /** @brief Initial fitting parameters. */
    std::vector<double> initial_values;
    /** @brief Whether to stop the fitting process. */
    bool stop_fitting;
};

/** @brief Data structure for storing status of the fitting result. */
struct FitStatus {
    /** @brief Used fitting method. */
    std::string method;
    /** @brief Number of iteration. */
    size_t num_iter;
    /** @brief Reason for stopping the iteration. */
    int info;
    /** @brief Initial cost, final cost, and final cond(J). */
    double chisq0, chisq, rcond;
};

/** @brief A class for fitting multiple Gaussian components to an image and generating model and residual images. */
class ImageFitter {
public:
    /** @brief Constructor for the ImageFitter class. */
    ImageFitter();
    /**
     * @brief Fit Gaussian components to an image and generate fitting results along with model and residual data.
     * @param width The width of the image
     * @param height The height of the image
     * @param image Pointer to the image data
     * @param beam_size Beam size of the image
     * @param unit Unit of the image
     * @param initial_values Initial fitting parameters
     * @param fixed_params Whether the fitting parameters are fixed
     * @param background_offset Background offset of the image
     * @param solver The type of solver to use.
     * @param create_model_image Whether to create a model image
     * @param create_residual_image Whether to create a residual image
     * @param fitting_response The fitting response message
     * @param progress_callback Callback function for updating fitting progress
     * @param offset_x X-axis offset from the fitting region to the entire image
     * @param offset_y Y-axis offset from the fitting region to the entire image
     * @return Whether the fitting is successful
     */
    bool FitImage(size_t width, size_t height, float* image, double beam_size, string unit,
        const std::vector<CARTA::GaussianComponent>& initial_values, const std::vector<bool>& fixed_params, double background_offset,
        CARTA::FittingSolverType solver, bool create_model_image, bool create_residual_image, CARTA::FittingResponse& fitting_response,
        GeneratorProgressCallback progress_callback, size_t offset_x = 0, size_t offset_y = 0);
    /**
     * @brief Generate model and residual images based on the fitting results.
     * @param image Pointer to the casacore ImageInterface object
     * @param image_region The fitting region
     * @param file_id ID of the fitting image file
     * @param filename Name of the fitting image file
     * @param model_image The generated model image
     * @param residual_image The generated residual image
     * @param fitting_response The fitting response message
     * @return Whether the images are successfully generated
     */
    bool GetGeneratedImages(std::shared_ptr<casacore::ImageInterface<float>> image, const casacore::ImageRegion& image_region, int file_id,
        const std::string& filename, GeneratedImage& model_image, GeneratedImage& residual_image, CARTA::FittingResponse& fitting_response);
    /** @brief Stop the ongoing fitting process. */
    void StopFitting();

private:
    /** @brief Fitting-related data. */
    FitData _fit_data;
    /** @brief Standard deviation of the image data. */
    double _image_std;
    /** @brief Beam size of the image. */
    double _beam_size;
    /** @brief Unit of the image. */
    string _unit;
    /** @brief Number of Gaussian components. */
    size_t _num_components;
    /** @brief Fitting parameter values. */
    gsl_vector* _fit_values;
    /** @brief Fitting parameter errors. */
    gsl_vector* _fit_errors;
    /** @brief Integrated flux values of components. */
    std::vector<double> _integrated_flux_values;
    /** @brief Integrated flux errors of components. */
    std::vector<double> _integrated_flux_errors;
    /** @brief Object for the fitting model. */
    gsl_multifit_nlinear_fdf _fdf;
    /** @brief Status of the fitting result. */
    FitStatus _fit_status;
    /** @brief Whether to create a model image. */
    bool _create_model_data;
    /** @brief Whether to create a residual image. */
    bool _create_residual_data;
    /** @brief Model image data. */
    std::vector<float> _model_data;
    /** @brief Residual image data. */
    std::vector<float> _residual_data;
    /** @brief Maximum number of fitting iterations. */
    const size_t _max_iter = 200;
    /** @brief Callback function for updating fitting progress. */
    GeneratorProgressCallback _progress_callback;

    /**
     * @brief Calculate the number of NaN values and standard deviation of the image data.
     */
    void CalculateNanNumAndStd();
    /**
     * @brief Set initial fitting parameters for the fitting.
     * @param initial_values Initial fitting parameters
     * @param background_offset Background offset of the image
     * @param fixed_params Whether the fitting parameters are fixed
     */
    void SetInitialValues(
        const std::vector<CARTA::GaussianComponent>& initial_values, double background_offset, const std::vector<bool>& fixed_params);
    /**
     * @brief Main function for the multiple Gaussian image fitting.
     * @param solver The type of solver to use
     * @return The status of the fitting
     */
    int SolveSystem(CARTA::FittingSolverType solver);
    /** @brief Calculate parameter errors after fitting. */
    void CalculateErrors();
    /**
     * @brief Calculate the model and residual image data after fitting.
     * @param residual Pointer to the residual values
     */
    void CalculateImageData(const gsl_vector* residual);
    /**
     * @brief Retrieve a log message describing the fitting status.
     * @return The log message
     */
    std::string GetLog();
    /**
     * @brief Generate a casacore ImageInterface object from the provided image data.
     * @param image The casacore ImageInterface object of the entire image
     * @param image_region The fitting region
     * @param image_data The image data for the generated casacore ImageInterface object
     * @return A casacore ImageInterface object
     */
    casa::SPIIF GetImageData(casa::SPIIF image, const casacore::ImageRegion& image_region, std::vector<float> image_data);
    /**
     * @brief Generate filenames by adding a suffix.
     * @param filename Name of the fitting image file
     * @param suffix The suffix to add
     * @return The modified filename
     */
    std::string GetFilename(const std::string& filename, std::string suffix);
    /**
     * @brief Generate filenames by adding a suffix for generated moment images.
     * @param filename Name of the fitting image file
     * @param suffix The suffix to add
     * @return The modified filename
     */
    std::string GetGeneratedMomentFilename(const std::string& filename, std::string suffix);

    /**
     * @brief Calculate the residual of the image data with the provided fitting parameters.
     * @param fit_params Fitting parameters
     * @param fit_data Fitting-related data
     * @param f The residual of the image data
     */
    static int FuncF(const gsl_vector* fit_params, void* fit_data, gsl_vector* f);
    /**
     * @brief Called after each iteration of the fitting.
     * @param iter The current iteration number
     * @param params Fitting parameters
     * @param w The workspace for the fitting
     */
    static void Callback(const size_t iter, void* params, const gsl_multifit_nlinear_workspace* w);
    /**
     * @brief Customize GSL errors.
     * @param reason The error reason
     * @param file The source file where the error occurred
     * @param line The line number where the error occurred
     * @param gsl_errno The GSL error number
     */
    static void ErrorHandler(const char* reason, const char* file, int line, int gsl_errno);
    /**
     * @brief Extract Gaussian parameters from a vector.
     * @param value_vector The vector containing fitting parameters
     * @param index The index of the Gaussian component
     * @param fit_values_indexes Indexes of the Gaussian parameters in the fittig parameters
     * @param initial_values Initial fitting parameter
     * @param offset_x X-axis offset from the fitting region to the entire image
     * @param offset_y Y-axis offset from the fitting region to the entire image
     * @return A tuple of Gaussian parameters
     */
    static std::tuple<double, double, double, double, double, double> GetGaussianParams(const gsl_vector* value_vector, size_t index,
        std::vector<int>& fit_values_indexes, std::vector<double>& initial_values, size_t offset_x = 0, size_t offset_y = 0);
    /**
     * @brief Create a Gaussian component sub-message from Gaussian parameters.
     * @param params Tuple containing Gaussian parameters
     * @return A Gaussian component sub-message
     */
    static CARTA::GaussianComponent GetGaussianComponent(std::tuple<double, double, double, double, double, double> params);
    /**
     * @brief Calculate the Median Absolute Deviation (MAD) of an array of data.
     * @param n The number of data points in the array.
     * @param x An array containing the data points.
     * @return The calculated MAD.
     */
    static double GetMedianAbsDeviation(const size_t n, double x[]);
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEFITTER_IMAGEFITTER_H_
