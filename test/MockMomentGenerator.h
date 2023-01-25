/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_TEST_MOCKMOMENTGENERATOR_H
#define CARTA_TEST_MOCKMOMENTGENERATOR_H

#include "gmock/gmock.h"

#include "ImageGenerators/MomentGenerator.h"

namespace carta {

// This is (currently) in the casacore object hierarchy, so we are inheriting from an implementation and not an abstract interface.

class MockMomentGenerator : public MomentGenerator {
public:
    MockMomentGenerator() : MomentGenerator("", nullptr) {}
    MOCK_METHOD(bool, CalculateMoments,
        (int file_id, const casacore::ImageRegion& image_region, int spectral_axis, int stokes_axis, int name_index,
            const GeneratorProgressCallback& progress_callback, const CARTA::MomentRequest& moment_request,
            CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results, const RegionState& region_state,
            const std::string& stokes),
        (override));
    MOCK_METHOD(void, StopCalculation, (), (override));
    MOCK_METHOD(bool, IsSuccess, (), (const, override));
    MOCK_METHOD(bool, IsCancelled, (), (const, override));
    MOCK_METHOD(casacore::String, GetErrorMessage, (), (const, override));
    MOCK_METHOD(void, setStepCount, (int count), (override));
    MOCK_METHOD(void, setStepsCompleted, (int count), (override));
    MOCK_METHOD(void, done, (), (override));
};

} // namespace carta

#endif // CARTA_TEST_MOCKMOMENTGENERATOR_H
