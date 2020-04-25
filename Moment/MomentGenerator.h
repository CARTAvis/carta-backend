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
    casacore::String output_filename;
    bool temporary;
    std::shared_ptr<casacore::ImageInterface<float>> image;

    CollapseResult(const String& output_filename_, bool temporary_, std::shared_ptr<ImageInterface<Float>> image_) {
        output_filename = output_filename_;
        temporary = temporary_;
        image = image_;
    }
};

class MomentGenerator {
public:
    MomentGenerator(casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis, const CARTA::MomentRequest& moment_request);
    ~MomentGenerator();

    void ExecuteMomentGenerator();
    bool IsSuccess() const;
    void SetOutputFileName(std::string output_filename);
    casacore::String GetErrorMessage() const;
    std::vector<CollapseResult> GetResults() const;

private:
    Record MakeRegionRecord(casacore::ImageInterface<float>* image, const CARTA::MomentRequest& moment_request);
    void SetPixelRange(const CARTA::MomentRequest& moment_request);
    int GetMomentMode(CARTA::Moment moment);
    String GetStokes(CARTA::MomentStokes moment_stokes);
    bool GetOutputFileName(casacore::String& out_name, int moment, const casacore::String& channel) const;

    casa::ImageMoments<float>* _image_moments;
    casacore::Vector<int> _moments;
    int _axis;                  // Not available yet, by default using the spectral axis
    casacore::String _channels; // For the output file name
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _base_name;                   // For the output file name
    std::string _output_filename;                  // For the output file name
    std::vector<CollapseResult> _collapse_results; // Moments calculation results
    casacore::String _error_msg;
    bool _collapse_error;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
