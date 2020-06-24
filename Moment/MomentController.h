#ifndef CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
#define CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_

#include "../Frame.h"
#include "MomentGenerator.h"

namespace carta {

class MomentController {
public:
    MomentController();
    ~MomentController();

    std::vector<CollapseResult> CalculateMoments(int file_id, const std::shared_ptr<Frame>& frame, MomentProgressCallback progress_callback,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
    std::vector<CollapseResult> CalculateMoments(int file_id, const std::shared_ptr<Frame>& frame,
        const casacore::ImageRegion& image_region, MomentProgressCallback progress_callback, const CARTA::MomentRequest& moment_request,
        CARTA::MomentResponse& moment_response);
    void StopCalculation(int file_id);

private:
    std::unordered_map<int, std::unique_ptr<MomentGenerator>> _moment_generators; // <file_id, MomentGenerator>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
