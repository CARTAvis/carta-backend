#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>
#include <imageanalysis/Regions/CasacRegionManager.h>

#include "../InterfaceConstants.h"

namespace carta {

class MomentGenerator : public casa::ImageMomentsProgressMonitor {
public:
    MomentGenerator(const String& filename, casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis,
        const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
    ~MomentGenerator();

    bool IsSuccess() const;
    casacore::String GetErrorMessage() const;

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

private:
    Record MakeRegionRecord(casacore::ImageInterface<float>* image, const CARTA::MomentRequest& moment_request);
    void ExecuteMomentGenerator(const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response);
    void SetMomentTypes(const CARTA::MomentRequest& moment_request);
    void SetPixelRange(const CARTA::MomentRequest& moment_request);
    int GetMomentMode(CARTA::Moment moment);
    String GetMomentSuffix(casacore::Int moment);
    String GetStokes(CARTA::MomentStokes moment_stokes);
    String GetOutputFileName();

    casa::ImageMoments<float>* _image_moments;
    casacore::Vector<casacore::Int> _moments;
    int _axis;
    String _filename;
    String _channels;
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _error_msg;
    bool _collapse_error;

    // Progress parameters
    int _total_steps;
    float _progress;
    float _pre_progress;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
