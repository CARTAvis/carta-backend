#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>
#include <imageanalysis/Regions/CasacRegionManager.h>

#include "../InterfaceConstants.h"

typedef const std::function<void(float)> MomentProgressCallback;

namespace carta {

struct CollapseResult {
    casacore::Int moment_type;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image;
    CollapseResult(casacore::Int moment_type_, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image_) {
        moment_type = moment_type_;
        image = image_;
    }
};

class MomentGenerator : public casa::ImageMomentsProgressMonitor {
public:
    MomentGenerator(const casacore::String& filename, casacore::ImageInterface<float>* image, std::string root_folder,
        casacore::String temp_folder, int spectral_axis, int stokes_axis, MomentProgressCallback progress_callback);
    ~MomentGenerator();

    void CalculateMoments(
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, bool write_results_to_disk = true);
    bool IsSuccess() const;
    casacore::String GetErrorMessage() const;
    std::vector<CollapseResult> GetCollapseResults();

    // Methods from the casa::ImageMomentsProgressMonitor interface
    void setStepCount(int count);
    void setStepsCompleted(int count);
    void done();

    // Print protobuf messages
    static void Print(CARTA::MomentRequest message);
    static void Print(CARTA::MomentResponse message);
    static void Print(CARTA::IntBounds message);
    static void Print(CARTA::FloatBounds message);
    static void Print(CARTA::MomentImage message);
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
    void DeleteImageMoments();
    int GetMomentMode(CARTA::Moment moment);
    casacore::String GetMomentSuffix(casacore::Int moment);
    casacore::String GetStokes(CARTA::MomentStokes moment_stokes);
    casacore::String GetOutputFileName();
    void RemoveRootFolder(std::string& directory);

    // Image parameters
    casacore::String _filename;
    std::string _root_folder;
    casacore::ImageInterface<float>* _image;
    int _spectral_axis;
    int _stokes_axis;

    // Moment settings
    casacore::ImageInterface<casacore::Float>* _sub_image;
    casa::ImageMoments<float>* _image_moments;
    casacore::Vector<casacore::Int> _moments;
    int _axis;
    casacore::String _channels;
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _error_msg;
    bool _collapse_error;

    // Progress parameters
    int _total_steps;
    float _progress;
    float _pre_progress;
    MomentProgressCallback _progress_callback;

    // Temporary folder to save temporary moment images
    casacore::String _temp_folder;

    // Calculation results (Moment images)
    std::vector<CollapseResult> _collapse_results;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
