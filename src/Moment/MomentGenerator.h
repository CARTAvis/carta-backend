/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
#define CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>

#include <chrono>
#include <thread>

#include "../Constants.h"
#include "ImageMoments.h"

using MomentProgressCallback = std::function<void(float)>;

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
    MomentGenerator(const casacore::String& filename, casacore::ImageInterface<float>* image);
    ~MomentGenerator(){};

    // Calculate moments
    bool CalculateMoments(int file_id, const casacore::ImageRegion& image_region, int spectral_axis, int stokes_axis,
        const MomentProgressCallback& progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
        std::vector<CollapseResult>& collapse_results);

    // Stop moments calculation
    void StopCalculation();

    // Resulting message
    bool IsSuccess() const;
    bool IsCancelled() const;
    casacore::String GetErrorMessage() const;

    // Methods from the "casa::ImageMomentsProgressMonitor" interface
    void setStepCount(int count);
    void setStepsCompleted(int count);
    void done();

private:
    void SetMomentAxis(const CARTA::MomentRequest& moment_request);
    void SetMomentTypes(const CARTA::MomentRequest& moment_request);
    void SetPixelRange(const CARTA::MomentRequest& moment_request);
    void ResetImageMoments(const casacore::ImageRegion& image_region);
    int GetMomentMode(CARTA::Moment moment);
    casacore::String GetMomentSuffix(casacore::Int moment);
    casacore::String GetOutputFileName();
    inline void SetMomentTypeMaps();

    // Image parameters
    casacore::String _filename;
    casacore::ImageInterface<float>* _image;
    int _spectral_axis;
    int _stokes_axis;

    // Moments settings
    std::unique_ptr<casacore::ImageInterface<casacore::Float>> _sub_image;
    std::unique_ptr<ImageMoments<casacore::Float>> _image_moments;
    casacore::Vector<casacore::Int> _moments; // Moment types
    int _axis;                                // Moment axis
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    casacore::String _error_msg;
    bool _success;
    bool _cancel;
    std::unordered_map<CARTA::Moment, ImageMoments<casacore::Float>::MomentTypes> _moment_map;
    std::unordered_map<ImageMoments<casacore::Float>::MomentTypes, casacore::String> _moment_suffix_map;

    // Progress parameters
    int _total_steps;
    float _progress;
    float _pre_progress;
    MomentProgressCallback _progress_callback;
    std::chrono::high_resolution_clock::time_point _start_time;
    bool _first_report_made;
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTGENERATOR_H_
