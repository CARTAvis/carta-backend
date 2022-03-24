/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "ImageFitter/ImageFitter.h"
#include "Logger/Logger.h"

#include "CommonTestUtilities.h"

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    float* GetImageCacheData() {
        return _image_cache.get();
    };
    size_t GetWidth() {
        return _width;
    };
    size_t GetHeight() {
        return _height;
    };
    FRIEND_TEST(ImageFittingTest, OneComponentFitting);
    FRIEND_TEST(ImageFittingTest, ThreeComponentFitting);
};

class ImageFittingTest : public ::testing::Test {
public:
    void SetInitialValues(std::vector<float> gaussian_model) {
        _initial_values = {};
        for (size_t i = 0; i < gaussian_model[0]; i++) {
            CARTA::GaussianComponent component;
            CARTA::Point center;
            center.set_x(gaussian_model[6 * i + 1]);
            center.set_y(gaussian_model[6 * i + 2]);
            *component.mutable_center() = center;
            component.set_amp(gaussian_model[6 * i + 3]);
            CARTA::Point fwhm;
            fwhm.set_x(gaussian_model[6 * i + 4]);
            fwhm.set_y(gaussian_model[6 * i + 5]);
            *component.mutable_fwhm() = fwhm;
            component.set_pa(gaussian_model[6 * i + 6]);
            _initial_values.push_back(component);
        }
    }

    void FitImage(std::vector<float> gaussian_model, std::string failed_message) {
        std::string gaussian_model_string = std::to_string(gaussian_model[0]);
        for (size_t i = 1; i < gaussian_model.size(); i++) {
            gaussian_model_string.append(" ");
            gaussian_model_string.append(std::to_string(gaussian_model[i]));
        }

        std::string file_path =
            ImageGenerator::GeneratedFitsImagePath("128 128", fmt::format("--gaussian-model {} -s 0", gaussian_model_string));
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));

        std::unique_ptr<carta::ImageFitter> image_fitter(
            new carta::ImageFitter(frame->GetWidth(), frame->GetHeight(), ""));
        bool success = image_fitter->FitImage(frame->GetImageCacheData(), _initial_values);
        std::string results = image_fitter->GetResults();

        if (failed_message.length() == 0) {
            EXPECT_EQ(success, True);
            for (size_t i = 0; i < gaussian_model[0]; i++) {
                std::string result = results.substr(results.find(fmt::format("Component #{}", i + 1)));
                EXPECT_EQ(GetFitParamFromResults(result, "Center X  = "), gaussian_model[6 * i + 1]);
                EXPECT_EQ(GetFitParamFromResults(result, "Center Y  = "), gaussian_model[6 * i + 2]);
                EXPECT_EQ(GetFitParamFromResults(result, "Amplitude = "), gaussian_model[6 * i + 3]);
                EXPECT_EQ(GetFitParamFromResults(result, "FWHM X    = "), gaussian_model[6 * i + 4]);
                EXPECT_EQ(GetFitParamFromResults(result, "FWHM Y    = "), gaussian_model[6 * i + 5]);
                EXPECT_EQ(GetFitParamFromResults(result, "P.A.      = "), gaussian_model[6 * i + 6]);
            }
        } else {
            EXPECT_EQ(success, False);
            EXPECT_EQ(image_fitter->GetMessage(), failed_message);
        }
    }

private:
    std::vector<CARTA::GaussianComponent> _initial_values;

    float GetFitParamFromResults(std::string results, std::string param) {
        std::string tmp = results.substr(results.find(param) + 12);
        return std::round(std::stof(tmp.substr(0, tmp.find(" +/-"))));
    }
};

TEST_F(ImageFittingTest, OneComponentFitting) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 45};
    SetInitialValues(gaussian_model);
    FitImage(gaussian_model, "");

    std::vector<float> bad_inital = {1, 64, 64, 20, 0, 0, 45};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}

TEST_F(ImageFittingTest, ThreeComponentFitting) {
    std::vector<float> gaussian_model = {3, 64, 64, 20, 20, 10, 120, 32, 32, 20, 20, 10, 120, 96, 96, 20, 20, 10, 120};
    SetInitialValues(gaussian_model);
    FitImage(gaussian_model, "");

    std::vector<float> bad_inital = {3, 64, 64, 20, 20, 10, 120, 64, 64, 20, 20, 10, 120, 96, 96, 20, 0, 0, 120};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}
