#ifndef CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
#define CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/moment_response.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>
#include <imageanalysis/Regions/CasacRegionManager.h>

#include "MomentGenerator.h"

namespace carta {

class MomentController {
public:
    MomentController();
    ~MomentController();
    void SetMomentGenerator(int file_id, const String& filename, casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis,
        const CARTA::MomentRequest& moment_request);
    std::vector<CollapseResult> GetResults(int file_id);

private:
    std::unordered_map<int, std::unique_ptr<MomentGenerator>> _moment_generator; // <file_id, MomentGenerator>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTCONTROLLER_H_
