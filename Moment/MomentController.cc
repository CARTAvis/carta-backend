#include "MomentController.h"

using namespace carta;

MomentController::MomentController() {}

MomentController::~MomentController() {
    DeleteMomentGenerator();
}

std::vector<CollapseResult> MomentController::CalculateMoments(int file_id, const std::shared_ptr<Frame>& frame,
    const casacore::ImageRegion& image_region, MomentProgressCallback progress_callback, const CARTA::MomentRequest& moment_request,
    CARTA::MomentResponse& moment_response) {
    std::vector<CollapseResult> results;

    // Set moment generator with respect to the file id
    if (!_moment_generators.count(file_id)) {
        // Create a moment generator
        auto moment_generator = std::make_unique<MomentGenerator>(
            frame->GetFileName(), frame->GetImage(), frame->GetSpectralAxis(), frame->GetStokesAxis(), progress_callback);

        std::unique_lock<std::mutex> lock(_moment_generator_mutex);
        _moment_generators[file_id] = std::move(moment_generator);
        lock.unlock();
    }

    // Calculate the moments
    if (_moment_generators.count(file_id)) {
        auto& moment_generators = _moment_generators.at(file_id);

        moment_generators->IncreaseMomentsCalcCount(); // Increase the moments calculation count (don't forget to decrease it after the
                                                       // calculation is done)
        results = moment_generators->CalculateMoments(file_id, image_region, moment_request, moment_response); // Do calculations
        moment_generators->DecreaseMomentsCalcCount(); // Decrease the moments calculation count
    }

    return results;
}

void MomentController::StopCalculation(int file_id) {
    if (_moment_generators.count(file_id)) {
        _moment_generators.at(file_id)->StopCalculation();
    }
}

void MomentController::DeleteMomentGenerator(int file_id) {
    if (_moment_generators.count(file_id)) {
        std::unique_lock<std::mutex> lock(_moment_generator_mutex);
        _moment_generators.at(file_id)->DisconnectCalled();
        _moment_generators[file_id].reset();
    }
}

void MomentController::DeleteMomentGenerator() {
    if (!_moment_generators.empty()) {
        std::unique_lock<std::mutex> lock(_moment_generator_mutex);
        for (auto& moment_generator : _moment_generators) {
            moment_generator.second->DisconnectCalled();
            moment_generator.second.reset();
        }
        _moment_generators.clear();
    }
}