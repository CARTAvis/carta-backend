/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    FRIEND_TEST(ImageFittingTest, OneComponentFitting);
    FRIEND_TEST(ImageFittingTest, ThreeComponentFitting);
};

class ImageFittingTest : public ::testing::Test {
public:
    void SetInitialValues(std::vector<float> gaussian_model) {
        _initial_values = {};
        for (size_t i = 0; i < gaussian_model[0]; i++) {
            CARTA::GaussianComponent component;
            CARTA::DoublePoint center;
            center.set_x(gaussian_model[6 * i + 1]);
            center.set_y(gaussian_model[6 * i + 2]);
            *component.mutable_center() = center;
            component.set_amp(gaussian_model[6 * i + 3]);
            CARTA::DoublePoint fwhm;
            fwhm.set_x(gaussian_model[6 * i + 4]);
            fwhm.set_y(gaussian_model[6 * i + 5]);
            *component.mutable_fwhm() = fwhm;
            component.set_pa(gaussian_model[6 * i + 6]);
            _initial_values.push_back(component);
        }
    }

    void FitImage(std::vector<float> gaussian_model, std::string failed_message = "") {
        std::string file_path = GetGeneratedFilePath(gaussian_model);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));

        CARTA::FittingResponse fitting_response;
        std::unique_ptr<carta::ImageFitter> image_fitter(new carta::ImageFitter());
        bool success =
            image_fitter->FitImage(frame->Width(), frame->Height(), frame->GetImageCacheData(), _initial_values, fitting_response);
        
        CompareResults(fitting_response, success, failed_message);
    }

private:
    std::vector<CARTA::GaussianComponent> _initial_values;

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

    void CompareResults(CARTA::FittingResponse fitting_response, bool success, std::string failed_message) {
        if (failed_message.length() == 0) {
            EXPECT_EQ(success, True);
            EXPECT_EQ(fitting_response.success(), True);
            for (size_t i = 0; i < _initial_values.size(); i++) {
                CARTA::GaussianComponent component = fitting_response.result_values(i);
                EXPECT_EQ(std::round(component.center().x()), _initial_values[i].center().x());
                EXPECT_EQ(std::round(component.center().y()), _initial_values[i].center().y());
                EXPECT_EQ(std::round(component.amp()), _initial_values[i].amp());
                EXPECT_EQ(std::round(component.fwhm().x()), _initial_values[i].fwhm().x());
                EXPECT_EQ(std::round(component.fwhm().y()), _initial_values[i].fwhm().y());
                EXPECT_EQ(std::round(component.pa()), _initial_values[i].pa());
            }
        } else {
            EXPECT_EQ(success, False);
            EXPECT_EQ(fitting_response.success(), False);
            EXPECT_EQ(fitting_response.message(), failed_message);
        }
    }
};

TEST_F(ImageFittingTest, OneComponentFitting) {
    std::vector<float> gaussian_model = {1, 64, 64, 20, 20, 10, 135};
    SetInitialValues(gaussian_model);
    FitImage(gaussian_model);

    std::vector<float> bad_inital = {1, 64, 64, 20, 0, 0, 135};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}

TEST_F(ImageFittingTest, ThreeComponentFitting) {
    std::vector<float> gaussian_model = {3, 64, 64, 20, 20, 10, 210, 32, 32, 20, 20, 10, 210, 96, 96, 20, 20, 10, 210};
    SetInitialValues(gaussian_model);
    FitImage(gaussian_model);

    std::vector<float> bad_inital = {3, 64, 64, 20, 20, 10, 210, 64, 64, 20, 20, 10, 210, 96, 96, 20, 0, 0, 210};
    SetInitialValues(bad_inital);
    FitImage(gaussian_model, "fit did not converge");
}
