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
    std::vector<CollapseResult> CalculateMoments2(int file_id, const std::unique_ptr<Frame>& frame,
        MomentProgressCallback progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
    void DeleteTemporaryFolder();

private:
    void MakeTemporaryFolder();

    std::string _root_folder;
    casacore::String _temp_folder;                                                // Temporary folder to save temporary moment images
    std::unordered_map<int, std::unique_ptr<MomentGenerator>> _moment_generators; // <file_id, MomentGenerator>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
