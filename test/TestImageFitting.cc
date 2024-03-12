/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#define SQ_FWHM_TO_SIGMA 1 / 8 / log(2)
#define DEG_TO_RAD M_PI / 180.0

#include <gtest/gtest.h>

#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "ImageFitter/ImageFitter.h"
#include "Logger/Logger.h"
#include "Region/RegionHandler.h"

#include "CommonTestUtilities.h"

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    float* GetImageCacheData() {
        return _image_cache.get();
    };
    FRIEND_TEST(ImageFittingTest, OneComponentFitting);
    FRIEND_TEST(ImageFittingTest, ThreeComponentFitting);
};

class ImageFittingTest : public ::testing::Test, public FileFinder {
public:
    void SetInitialValues(std::vector<float> gaussian_model) {
        _initial_values = {};
        for (size_t i = 0; i < gaussian_model[0]; i++) {
            auto center = Message::DoublePoint(gaussian_model[6 * i + 1], gaussian_model[6 * i + 2]);
            double amp = gaussian_model[6 * i + 3];
            auto fwhm = Message::DoublePoint(gaussian_model[6 * i + 4], gaussian_model[6 * i + 5]);
            double pa = gaussian_model[6 * i + 6];
            _initial_values.push_back(Message::GaussianComponent(center, amp, fwhm, pa));
        }
    }

    void SetFixedParams(std::vector<bool> fixed_params) {
        _fixed_params = fixed_params;
    }

    void SetFov(CARTA::RegionType region_type, std::vector<float> control_points, float rotation) {
        _fov_info.set_region_type(region_type);
        for (size_t i = 0; i < control_points.size() / 2; i++) {
            auto* control_point = _fov_info.add_control_points();
            control_point->set_x(control_points[i * 2]);
            control_point->set_y(control_points[i * 2 + 1]);
        }
        _fov_info.set_rotation(rotation);
    }

    void FitImage(std::vector<float> gaussian_model, std::string failed_message = "") {
        std::string file_path = GetGeneratedFilePath(gaussian_model);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));

        CARTA::FittingResponse fitting_response;
        std::unique_ptr<carta::ImageFitter> image_fitter(new carta::ImageFitter());
        auto progress_callback = [&](float progress) {};
        bool success = image_fitter->FitImage(frame->Width(), frame->Height(), frame->GetImageCacheData(), 0.0, "", _initial_values,
            _fixed_params, 0.0, CARTA::FittingSolverType::Cholesky, true, true, fitting_response, progress_callback);

        CompareResults(fitting_response, success, failed_message);

        if (failed_message.length() == 0) {
            GeneratedImage model_image;
            GeneratedImage residual_image;
            int file_id(0);
            StokesRegion output_stokes_region;
            frame->GetImageRegion(file_id, AxisRange(frame->CurrentZ()), frame->CurrentStokes(), output_stokes_region);
            casa::SPIIF image(loader->GetStokesImage(output_stokes_region.stokes_source));
            success = image_fitter->GetGeneratedImages(
                image, output_stokes_region.image_region, frame->GetFileName(), model_image, residual_image, fitting_response);

            EXPECT_TRUE(success);
            CompareImageResults(model_image, residual_image, fitting_response, frame->GetFileName(), frame->GetImageCacheData());
        }
    }

    void FitImageWithFov(std::vector<float> gaussian_model, int region_id, std::string failed_message = "") {
        std::string file_path = GetGeneratedFilePath(gaussian_model);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

        // TODO: avoid using higher level function region_handler.FitImage
        CARTA::FittingRequest fitting_request;
        fitting_request.set_file_id(0);
        fitting_request.set_region_id(region_id);
        for (size_t i = 0; i < _initial_values.size(); i++) {
            *fitting_request.add_initial_values() = _initial_values[i];
        }
        for (size_t i = 0; i < _fixed_params.size(); i++) {
            fitting_request.add_fixed_params(_fixed_params[i]);
        }
        *fitting_request.mutable_fov_info() = _fov_info;

        CARTA::FittingResponse fitting_response;
        carta::RegionHandler region_handler;
        GeneratedImage model_image;
        GeneratedImage residual_image;
        auto progress_callback = [&](float progress) {};
        bool success = region_handler.FitImage(fitting_request, fitting_response, frame, model_image, residual_image, progress_callback);

        // TODO: test generated model/residual images
        CompareResults(fitting_response, success, failed_message);
    }

private:
    std::vector<CARTA::GaussianComponent> _initial_values;
    std::vector<bool> _fixed_params;
    CARTA::RegionInfo _fov_info;

    static std::string GetGeneratedFilePath(std::vector<float> gaussian_model) {
        std::string gaussian_model_string = std::to_string(gaussian_model[0]);
        for (size_t i = 1; i < gaussian_model.size(); i++) {
            gaussian_model_string.append(" ");
            gaussian_model_string.append(i % 6 == 0 ? std::to_string(gaussian_model[i] - 90.0) : std::to_string(gaussian_model[i]));
        }

        std::string file_path =
            ImageGenerator::GeneratedFitsImagePath("128 128", fmt::format("--gaussian-model {} -s 0", gaussian_model_string));
        return file_path;
    }

    void CompareResults(const CARTA::FittingResponse fitting_response, const bool success, const std::string failed_message) {
        if (failed_message.length() == 0) {
            EXPECT_TRUE(success);
            EXPECT_TRUE(fitting_response.success());
            for (size_t i = 0; i < _initial_values.size(); i++) {
                CARTA::GaussianComponent component = fitting_response.result_values(i);
                EXPECT_EQ(std::round(component.center().x()), _initial_values[i].center().x());
                EXPECT_EQ(std::round(component.center().y()), _initial_values[i].center().y());
                EXPECT_EQ(std::round(component.amp()), _initial_values[i].amp());
                EXPECT_EQ(std::round(component.fwhm().x()), _initial_values[i].fwhm().x());
                EXPECT_EQ(std::round(component.fwhm().y()), _initial_values[i].fwhm().y());
                EXPECT_EQ(std::round(component.pa()), _initial_values[i].pa());
            }
            EXPECT_EQ(std::round(fitting_response.offset_value()), 0);
        } else {
            EXPECT_FALSE(success);
            EXPECT_FALSE(fitting_response.success());
            EXPECT_EQ(fitting_response.message(), failed_message);
        }
    }

    void CompareImageResults(const GeneratedImage model_image, const GeneratedImage residual_image,
        const CARTA::FittingResponse fitting_response, const std::string file_path, float* image) {
        std::string filename = file_path.substr(file_path.rfind('/') + 1);
        std::vector<float> model_data;
        GetImageData(model_data, model_image.image, 0);
        std::vector<float> residual_data;
        GetImageData(residual_data, residual_image.image, 0);
        std::vector<CARTA::GaussianComponent> result_values(
            fitting_response.result_values().begin(), fitting_response.result_values().end());
        std::vector<float> expect_model_data = Gaussian(result_values);

        EXPECT_EQ(model_image.name, filename.substr(0, filename.length() - 5) + "_model.fits");
        EXPECT_EQ(model_data.size(), 128 * 128);
        for (size_t i = 0; i < model_data.size(); i++) {
            EXPECT_NEAR(model_data[i], expect_model_data[i], 1e-6);
        }

        EXPECT_EQ(residual_image.name, filename.substr(0, filename.length() - 5) + "_residual.fits");
        EXPECT_EQ(residual_data.size(), 128 * 128);
        for (size_t i = 0; i < residual_data.size(); i++) {
            EXPECT_NEAR(residual_data[i], image[i] - expect_model_data[i], 1e-6);
        }
    }

    static std::vector<float> Gaussian(const std::vector<CARTA::GaussianComponent>& result_values) {
        std::vector<float> result(128 * 128, 0.0);
        size_t n = result_values.size();
        for (size_t k = 0; k < n; k++) {
            CARTA::GaussianComponent component = result_values[k];
            double center_x = component.center().x();
            double center_y = component.center().y();
            double amp = component.amp();
            double fwhm_x = component.fwhm().x();
            double fwhm_y = component.fwhm().y();
            double pa = component.pa();

            const double dbl_sq_std_x = 2 * fwhm_x * fwhm_x * SQ_FWHM_TO_SIGMA;
            const double dbl_sq_std_y = 2 * fwhm_y * fwhm_y * SQ_FWHM_TO_SIGMA;
            const double theta_radian = (pa - 90.0) * DEG_TO_RAD; // counterclockwise rotation
            const double a = cos(theta_radian) * cos(theta_radian) / dbl_sq_std_x + sin(theta_radian) * sin(theta_radian) / dbl_sq_std_y;
            const double dbl_b = 2 * (sin(2 * theta_radian) / (2 * dbl_sq_std_x) - sin(2 * theta_radian) / (2 * dbl_sq_std_y));
            const double c = sin(theta_radian) * sin(theta_radian) / dbl_sq_std_x + cos(theta_radian) * cos(theta_radian) / dbl_sq_std_y;

            for (size_t i = 0; i < 128; i++) {
                for (size_t j = 0; j < 128; j++) {
                    double dx = i - center_x;
                    double dy = j - center_y;
                    result[j * 128 + i] += amp * exp(-(a * dx * dx + dbl_b * dx * dy + c * dy * dy));
                }
            }
        }
        return result;
    }
};

TEST_F(ImageFittingTest, OneComponentFitting) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    std::vector<bool> fixed_params(6, false);
    fixed_params.push_back(true);
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    FitImage(gaussian_model);

    std::vector<float> bad_inital = {1, 64, 64, 20, 0, 0, 135};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}

TEST_F(ImageFittingTest, ThreeComponentFitting) {
    std::vector<float> gaussian_model = {3, 64, 64, 20, 20, 10, 210, 32, 32, 20, 20, 10, 210, 96, 96, 20, 20, 10, 210};
    std::vector<bool> fixed_params(18, false);
    fixed_params.push_back(true);
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    FitImage(gaussian_model);

    std::vector<float> bad_inital = {3, 64, 64, 20, 20, 10, 210, 64, 64, 20, 20, 10, 210, 96, 96, 20, 0, 0, 210};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}

TEST_F(ImageFittingTest, CenterFixedFitting) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    std::vector<bool> fixed_params = {true, true, false, false, false, false, true};
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    FitImage(gaussian_model);
}

TEST_F(ImageFittingTest, BackgroundUnfixedFitting) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    std::vector<bool> fixed_params = {false, false, false, false, false, false, false};
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    FitImage(gaussian_model);
}

TEST_F(ImageFittingTest, FittingWithFov) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    std::vector<bool> fixed_params(6, false);
    fixed_params.push_back(true);
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    SetFov(CARTA::RegionType::RECTANGLE, {63.5, 63.5, 96, 96}, 10);
    FitImageWithFov(gaussian_model, 0);
}

TEST_F(ImageFittingTest, IncorrectRegionId) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    FitImageWithFov(gaussian_model, IMAGE_REGION_ID, "region id not found");
    FitImageWithFov(gaussian_model, 1, "region id not found");
}

TEST_F(ImageFittingTest, IncorrectFov) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    FitImageWithFov(gaussian_model, 0, "failed to set up field of view region");

    SetFov(CARTA::RegionType::LINE, {0, 0, 1, 1}, 0);
    FitImageWithFov(gaussian_model, 0, "region is outside image or is not closed");
}

TEST_F(ImageFittingTest, FovOutsideImage) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    SetFov(CARTA::RegionType::RECTANGLE, {-100, -100, 10, 10}, 0);
    FitImageWithFov(gaussian_model, 0, "region is outside image or is not closed");
}

TEST_F(ImageFittingTest, insufficientData) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    std::vector<bool> fixed_params(6, false);
    fixed_params.push_back(true);
    SetInitialValues(gaussian_model);
    SetFixedParams(fixed_params);
    SetFov(CARTA::RegionType::RECTANGLE, {63.5, 63.5, 2, 2}, 0);
    FitImageWithFov(gaussian_model, 0, "insufficient data points");
}

TEST_F(ImageFittingTest, RunImageFitter) {
    std::string file_path = FitsImagePath("spw25_mom0.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
    std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));

    std::vector<float> gaussian_model = {1, 21.364, 24.199, 77, 5, 5, 60}; // gauss num, x, y, amp, fwhm-x, fwhm-y, pa
    auto center = Message::DoublePoint(gaussian_model[1], gaussian_model[2]);
    double amp = gaussian_model[3];
    auto fwhm = Message::DoublePoint(gaussian_model[4], gaussian_model[5]);
    double pa = gaussian_model[6];
    std::vector<CARTA::GaussianComponent> initial_values;
    initial_values.push_back(Message::GaussianComponent(center, amp, fwhm, pa));

    std::vector<bool> fixed_params(7, false);

    CARTA::FittingResponse fitting_response;
    std::unique_ptr<carta::ImageFitter> image_fitter(new carta::ImageFitter());
    auto progress_callback = [&](float progress) {};
    bool success = image_fitter->FitImage(frame->Width(), frame->Height(), frame->GetImageCacheData(), 0.0, "", initial_values,
        fixed_params, 0.0, CARTA::FittingSolverType::Cholesky, true, true, fitting_response, progress_callback);

    std::cout << "Fit success = " << fitting_response.success() << "\n";

    auto fit_results = fitting_response.result_values();
    std::cout << "\nFit results: \n";
    std::cout << "x = " << fit_results[0].center().x() << "\n";
    std::cout << "y = " << fit_results[0].center().y() << "\n";
    std::cout << "amp = " << fit_results[0].amp() << "\n";
    std::cout << "fwhm-x = " << fit_results[0].fwhm().x() << "\n";
    std::cout << "fwhm-y = " << fit_results[0].fwhm().y() << "\n";
    std::cout << "pa = " << fit_results[0].pa() << "\n";

    std::cout << "\nFit errors: \n";
    auto fit_errors = fitting_response.result_errors();
    std::cout << "x = " << fit_errors[0].center().x() << "\n";
    std::cout << "y = " << fit_errors[0].center().y() << "\n";
    std::cout << "amp = " << fit_errors[0].amp() << "\n";
    std::cout << "fwhm-x = " << fit_errors[0].fwhm().x() << "\n";
    std::cout << "fwhm-y = " << fit_errors[0].fwhm().y() << "\n";
    std::cout << "pa = " << fit_errors[0].pa() << "\n";
}

TEST_F(ImageFittingTest, RunImageFitter2) {
    casacore::ImageInterface<casacore::Float>* image;
    std::string file_path = FitsImagePath("spw25_mom0.fits");
    casacore::ImageUtilities::openImage(image, file_path);
    casa::SPCIIF input_image(image);
    carta::ImageFitter2<casacore::Float> image_fitter2(input_image, "", 0, "", "", "", "", "", "");
    image_fitter2.setModel("");
    image_fitter2.setResidual("");
    image_fitter2.fit();
}

TEST_F(ImageFittingTest, TestDeconvolver) {
    casacore::ImageInterface<casacore::Float>* image;
    std::string file_path = FitsImagePath("spw25_mom0.fits");
    casacore::ImageUtilities::openImage(image, file_path);
    casa::SPIIF input_image(image);
    carta::Deconvolver deconvolver(input_image);

    CARTA::GaussianComponent in_gauss;
    in_gauss.set_amp(77.8518);                 // kJy.m.s-1/beam
    in_gauss.mutable_center()->set_x(21.3636); // in pixel coordinate
    in_gauss.mutable_center()->set_y(24.199);  // in pixel coordinate
    in_gauss.mutable_fwhm()->set_x(10.9295);   // in pixel coordinate
    in_gauss.mutable_fwhm()->set_y(9.14887);   // in pixel coordinate
    in_gauss.set_pa(2.62175);                  // 2.62175 (rad) = 150.21 (degree) => 150.21 - 90 = 60.21 (degree)

    int chan(0);
    int stokes(0);
    std::shared_ptr<casa::GaussianShape> out_gauss;
    bool success = deconvolver.DoDeconvolution(chan, stokes, in_gauss, out_gauss);
    EXPECT_TRUE(success);

    if (success) {
        std::string major = fmt::format("{:.3f}", out_gauss->majorAxis().getValue());
        std::string minor = fmt::format("{:.3f}", out_gauss->minorAxis().getValue());
        std::string pa = fmt::format("{:.0f}", out_gauss->positionAngle().getValue());

        std::string err_major = fmt::format("{:.3f}", out_gauss->majorAxisError().getValue());
        std::string err_minor = fmt::format("{:.3f}", out_gauss->minorAxisError().getValue());
        std::string err_pa = fmt::format("{:.0f}", out_gauss->positionAngleError().getValue());

        EXPECT_EQ(major, "1.273");
        EXPECT_EQ(minor, "1.167");
        EXPECT_EQ(pa, "25");

        EXPECT_EQ(err_major, "0.070");
        EXPECT_EQ(err_minor, "0.071");
        EXPECT_EQ(err_pa, "31");

        std::string unit_major = out_gauss->majorAxis().getUnit();
        std::string unit_minor = out_gauss->minorAxis().getUnit();
        std::string unit_pa = out_gauss->positionAngle().getUnit();

        std::string unit_err_major = out_gauss->majorAxisError().getUnit();
        std::string unit_err_minor = out_gauss->minorAxisError().getUnit();
        std::string unit_err_pa = out_gauss->positionAngleError().getUnit();

        EXPECT_EQ(unit_major, "arcsec");
        EXPECT_EQ(unit_minor, "arcsec");
        EXPECT_EQ(unit_pa, "deg");

        EXPECT_EQ(unit_err_major, "arcsec");
        EXPECT_EQ(unit_err_minor, "arcsec");
        EXPECT_EQ(unit_err_pa, "deg");

        std::cout << " --- major axis FWHM = " << out_gauss->majorAxis() << " +/- " << out_gauss->majorAxisError() << "\n";
        std::cout << " --- minor axis FWHM = " << out_gauss->minorAxis() << " +/- " << out_gauss->minorAxisError() << "\n";
        std::cout << " --- position angle = " << out_gauss->positionAngle() << " +/- " << out_gauss->positionAngleError() << "\n";
    }
}
