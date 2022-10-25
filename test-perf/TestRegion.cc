/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <omp.h>

#include "CommonTestUtilities.h"
#include "Frame/VectorFieldCalculator.h"
#include "ImageData/FileLoader.h"
#include "InitTester.h"
#include "TestFrame.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Timer/Timer.h"

using namespace carta;
using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 5) {
        spdlog::error(
            "Usage: "
            "./TestRegion "
            "<full path name of the image file> "
            "<omp thread count, -1 means auto selected> "
            "<verbosity, 4: info, 5:debug> "
            "<region width/height, -1 means equal to the image width and height>");
        return 1;
    }

    // Set image file name path
    string path_string(argv[1]);

    // Set logger
    InitSpdlog(argv[3]);

    // Set OMP thread numbers
    InitOmpThreads(argv[2]);

    // Set region width/height
    string region_width_height(argv[4]);
    int region_size = stoi(region_width_height);

    // Set FileLoader
    shared_ptr<FileLoader> loader(FileLoader::GetLoader(path_string));

    // Set Frame
    uint32_t session_id(0);
    shared_ptr<Frame> frame = make_shared<Frame>(session_id, loader, "0");

    if (!frame->IsValid()) {
        spdlog::error("Failed to open the image file!");
        return 1;
    }

    // Set image channels through the Frame
    std::string message;
    int channel(0), stokes(0);
    frame->SetImageChannels(channel, stokes, message);

    // Create a region handler
    auto region_handler = std::make_unique<carta::RegionHandler>();

    // Set a rectangle region state: // [(cx,cy), (width,height)], width/height > 0
    int region_id(1);
    int image_width = frame->Width();
    int image_height = frame->Height();
    int center_x(image_width / 2);
    int center_y(image_height / 2);
    int region_width = region_size > 0 ? region_size : image_width;
    int region_height = region_size > 0 ? region_size : image_height;
    spdlog::debug("Set region width/height: {}/{}", region_width, region_height);
    std::vector<CARTA::Point> points = {
        Message::Point(center_x, center_y), Message::Point(region_width, region_height)}; // {center, width/height}

    int ref_file_id(0);
    RegionState region_state(ref_file_id, CARTA::RegionType::RECTANGLE, points, 0);
    EXPECT_TRUE(region_handler->SetRegion(region_id, region_state, frame->CoordinateSystem()));

    // Set spectral configs for a rectangle region
    std::string stokes_config_z("z");
    int file_id(0);
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
    region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

    // Get cursor spectral profile data from the RegionHandler
    CARTA::SpectralProfile spectral_profile;
    bool stokes_changed(true);
    auto callback = [&](CARTA::SpectralProfileData tmp_spectral_profile) {
        if (tmp_spectral_profile.progress() >= 1.0) {
            spectral_profile = tmp_spectral_profile.profiles(0);
        }
    };

    carta::Timer t;
    region_handler->FillSpectralProfileData(callback, region_id, file_id, stokes_changed);
    spdlog::info("Elapsed time to calculate the region spectral profile data: {} (ms)", t.Elapsed().ms());

    auto spectral_profile_as_double = GetSpectralProfileValues<double>(spectral_profile);

    // convert the double type vector to the float type vector
    std::vector<float> spectral_profile_as_float(spectral_profile_as_double.begin(), spectral_profile_as_double.end());
    EXPECT_GT(spectral_profile_as_float.size(), 0);

    return 0;
}
