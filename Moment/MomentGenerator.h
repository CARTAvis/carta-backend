#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/moment_response.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>
#include <imageanalysis/Regions/CasacRegionManager.h>

#include "../InterfaceConstants.h"

namespace carta {

struct CollapseResult {
    int moment_type;
    std::shared_ptr<casacore::ImageInterface<float>> image;
    casacore::String output_filename;

    CollapseResult(int moment_type_, std::shared_ptr<ImageInterface<Float>> image_, const String& output_filename_) {
        moment_type = moment_type_;
        image = image_;
        output_filename = output_filename_;
    }
};

class MomentGenerator {
public:
    MomentGenerator(casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis, const CARTA::MomentRequest& moment_request);
    ~MomentGenerator();

    void ExecuteMomentGenerator();
    bool IsSuccess() const;
    casacore::String GetErrorMessage() const;
    std::vector<CollapseResult> GetResults() const;

private:
    Record MakeRegionRecord(casacore::ImageInterface<float>* image, const CARTA::MomentRequest& moment_request);
    void SetPixelRange(const CARTA::MomentRequest& moment_request);
    int GetMomentMode(CARTA::Moment moment);
    String GetStokes(CARTA::MomentStokes moment_stokes);

    casa::ImageMoments<float>* _image_moments;
    casacore::Vector<int> _moments;
    int _axis; // Not available yet, by default using the spectral axis
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    std::vector<CollapseResult> _collapse_results; // Moments calculation results
    casacore::String _error_msg;
    bool _collapse_error;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
