#include "MomentController.h"

using namespace carta;

MomentController::MomentController(std::string root_folder) : _root_folder(root_folder) {}

MomentController::~MomentController() {
    for (auto& moment_generator : _moment_generators) {
        moment_generator.second.reset(); // delete Frame
    }
    _moment_generators.clear();
}

void MomentController::CalculateMoments(int file_id, const std::unique_ptr<Frame>& frame, MomentProgressCallback progress_callback,
    const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response) {
    // Set moment generator with respect to the file id
    if (!_moment_generators.count(file_id)) {
        // Get the image ptr and spectral/stoke axis from the Frame
        std::string filename = frame->GetFileName();
        casacore::ImageInterface<float>* image = frame->GetImage();
        int spectral_axis = frame->GetSpectralAxis();
        int stokes_axis = frame->GetStokesAxis();

        // Create a moment generator
        auto moment_generator =
            std::make_unique<MomentGenerator>(filename, image, _root_folder, spectral_axis, stokes_axis, progress_callback);
        _moment_generators[file_id] = std::move(moment_generator);
    }

    // Calculate the moments
    if (_moment_generators.count(file_id)) {
        _moment_generators.at(file_id)->CalculateMoments(moment_request, moment_response);
    }
}
