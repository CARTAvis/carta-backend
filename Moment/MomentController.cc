#include "MomentController.h"

using namespace carta;

MomentController::MomentController() {}
MomentController::~MomentController() {}

void MomentController::SetMomentGenerator(
    int file_id, casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis, const CARTA::MomentRequest& moment_request) {
    auto moment_generator = std::unique_ptr<MomentGenerator>(new MomentGenerator(image, spectral_axis, stokes_axis, moment_request));
    _moment_generator[file_id] = move(moment_generator);
}