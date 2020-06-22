#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>

#include "../Analysis/CasacRegionManager.h"
#include "../Analysis/ImageMoments.h"
#include "../Analysis/SubImageFactory.h"
#include "../InterfaceConstants.h"

using MomentProgressCallback = const std::function<void(float)>;

namespace carta {

struct CollapseResult {
    int file_id;
    std::string name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image;
    CollapseResult(int file_id_, std::string name_, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image_) {
        file_id = file_id_;
        name = name_;
        image = image_;
    }
};

class MomentGenerator : public casa::ImageMomentsProgressMonitor {
public:
    MomentGenerator(const casacore::String& filename, casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis,
        MomentProgressCallback progress_callback);
    ~MomentGenerator(){};

    // Calculate moments
    std::vector<CollapseResult> CalculateMoments(
        int file_id, int current_stokes, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);

    // Stop moments calculation
    void StopCalculation();

    // Resulting message
    bool IsSuccess() const;
    casacore::String GetErrorMessage() const;

    // Methods from the "casa::ImageMomentsProgressMonitor" interface
    void setStepCount(int count);
    void setStepsCompleted(int count);
    void done();

    // Print protobuf messages
    static void Print(CARTA::MomentRequest message);
    static void Print(CARTA::MomentResponse message);
    static void Print(CARTA::IntBounds message);
    static void Print(CARTA::FloatBounds message);
    static void Print(CARTA::Moment message);
    static void Print(CARTA::MomentAxis message);
    static void Print(CARTA::MomentStokes message);
    static void Print(CARTA::MomentMask message);
    static void Print(CARTA::MomentProgress message);

private:
    void SetMomentAxis(const CARTA::MomentRequest& moment_request);
    void SetMomentTypes(const CARTA::MomentRequest& moment_request);
    void SetPixelRange(const CARTA::MomentRequest& moment_request);
    casacore::Record MakeRegionRecord(const CARTA::MomentRequest& moment_request);
    void ResetImageMoments(const CARTA::MomentRequest& moment_request);
    int GetMomentMode(CARTA::Moment moment);
    casacore::String GetMomentSuffix(casacore::Int moment);
    casacore::String GetStokes(CARTA::MomentStokes moment_stokes);
    casacore::String GetOutputFileName();

    // Image parameters
    casacore::String _filename;
    casacore::ImageInterface<float>* _image;
    int _spectral_axis;
    int _stokes_axis;
    int _current_stokes;

    // Moment settings
    std::unique_ptr<casacore::ImageInterface<casacore::Float>> _sub_image;
    std::unique_ptr<ImageMoments<casacore::Float>> _image_moments;
    casacore::Vector<casacore::Int> _moments; // Moment types
    int _axis;                                // Moment axis
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _error_msg;
    bool _collapse_error;

    // Progress parameters
    int _total_steps;
    float _progress;
    float _pre_progress;
    MomentProgressCallback _progress_callback;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
