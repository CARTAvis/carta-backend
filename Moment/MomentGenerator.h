#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>
#include <imageanalysis/Regions/CasacRegionManager.h>

#include "../Analysis/ImageMoments.h"
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
    ~MomentGenerator();

    // Calculate moments
    bool ApplyStoppableMomentsCalculation();
    std::vector<CollapseResult> CalculateMoments(
        int file_id, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
    std::vector<CollapseResult> CalculateMomentsStoppable(
        int file_id, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
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
    casacore::Record MakeRegionRecord(const CARTA::MomentRequest& moment_request, const casacore::String& pixel_box);
    void ResetImageMoments(const CARTA::MomentRequest& moment_request);
    void ResetSubImageMoments(const CARTA::MomentRequest& moment_request, const std::vector<casacore::Int>& pixel_box);
    int GetMomentMode(CARTA::Moment moment);
    casacore::String GetMomentSuffix(casacore::Int moment);
    casacore::String GetStokes(CARTA::MomentStokes moment_stokes);
    casacore::String GetOutputFileName();
    void InitCollapseResults(int file_id);
    casacore::CoordinateSystem MakeOutputCoordinates(casacore::IPosition& out_shape, const casacore::CoordinateSystem& in_coor_sys,
        const casacore::IPosition& in_shape, casacore::Int moment_axis, casacore::Bool remove_axis);
    std::vector<std::vector<casacore::Int>> GetPixelBoxes();

    // Image parameters
    casacore::String _filename;
    casacore::ImageInterface<float>* _image;
    int _dir_x_axis;
    int _dir_y_axis;
    int _spectral_axis;
    int _stokes_axis;

    // Moment settings
    std::unique_ptr<casacore::ImageInterface<casacore::Float>> _sub_image;
    std::unique_ptr<ImageMoments<casacore::Float>> _image_moments;
    casacore::Vector<casacore::Int> _moments;
    int _axis;
    casacore::String _channels;
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _error_msg;
    bool _collapse_error;

    // Progress parameters
    bool _use_default_progress_reporter;
    int _total_steps;
    float _progress;
    float _pre_progress;
    MomentProgressCallback _progress_callback;

    // Calculation results
    std::vector<CollapseResult> _collapse_results;

    // Stop moment calculation
    volatile bool _stop;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
