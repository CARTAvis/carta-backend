#ifndef CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
#define CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_

#include "../Frame.h"
#include "MomentGenerator.h"

namespace carta {

class MomentController {
public:
    MomentController();
    ~MomentController();

    std::vector<CollapseResult> CalculateMoments(int file_id, const std::shared_ptr<Frame>& frame,
        const casacore::ImageRegion& image_region, MomentProgressCallback progress_callback, const CARTA::MomentRequest& moment_request,
        CARTA::MomentResponse& moment_response);
    void StopCalculation(int file_id);
    void DeleteMomentGenerator(int file_id); // Delete a moment generator with respect to the file Id
    void DeleteMomentGenerator();            // Delete all moment generators;

private:
    std::unordered_map<int, std::unique_ptr<MomentGenerator>> _moment_generators; // <file_id, MomentGenerator>
    std::unordered_map<int, std::mutex> _image_mutexes;                           // <file_id, mutex for MomentGenerator>
    std::mutex _moment_generator_mutex;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
