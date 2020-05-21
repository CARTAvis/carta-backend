#ifndef CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
#define CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_

#include "../Frame.h"
#include "MomentGenerator.h"

namespace carta {

class MomentController {
public:
    MomentController(std::string root_folder);
    ~MomentController();

    void CalculateMoments(int file_id, const std::unique_ptr<Frame>& frame, MomentProgressCallback progress_callback,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);

private:
    std::string _root_folder;
    std::unordered_map<int, std::unique_ptr<MomentGenerator>> _moment_generators; // <file_id, MomentGenerator>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
