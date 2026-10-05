/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_MOMENTGENERATOR_H_
#define CARTA_SRC_IMAGEGENERATORS_MOMENTGENERATOR_H_

#include <cstdint>

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <imageanalysis/ImageAnalysis/ImageMomentsProgressMonitor.h>

#include <chrono>
#include <thread>

#include "ImageGenerator.h"
#include "ImageMoments.h"
#include "Region/Region.h"

#define FIRST_PROGRESS_AFTER_MILLI_SECS 5000
#define PROGRESS_REPORT_INTERVAL 0.1

namespace carta {

class MomentGenerator : public casa::ImageMomentsProgressMonitor {
public:
    MomentGenerator(const casacore::String& filename, std::shared_ptr<casacore::ImageInterface<float>> image);
    ~MomentGenerator() = default;

    // Calculate moments
    bool CalculateMoments(int file_id, const casacore::ImageRegion& image_region, int spectral_axis, int stokes_axis, int name_index,
        const GeneratorProgressCallback& progress_callback, const CARTA::MomentRequest& moment_request,
        CARTA::MomentResponse& moment_response, std::vector<GeneratedImage>& collapse_results, const RegionState& region_state,
        const std::string& stokes);

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
    void SetRestFrequency(const CARTA::MomentRequest& moment_request);
    void ResetImageMoments(const casacore::ImageRegion& image_region);
    void SetImageRestFrequency(double rest_frequency);
    int GetMomentMode(CARTA::Moment moment);
    casacore::String GetMomentSuffix(casacore::Int moment);
    casacore::String GetInputFileName();
    inline void SetMomentTypeMaps();
    void SetMomentImageLogger(const CARTA::MomentRequest& moment_request, const RegionState& region_state, const std::string& stokes);

    // Image parameters
    casacore::String _filename;
    std::shared_ptr<casacore::ImageInterface<float>> _image;
    int _spectral_axis;
    int _stokes_axis;
    casacore::LoggerHolder _logger;

    // Moments settings
    std::unique_ptr<casacore::ImageInterface<casacore::Float>> _sub_image;
    std::unique_ptr<ImageMoments<casacore::Float>> _image_moments;
    casacore::Vector<casacore::Int> _moments; // Moment types
    int _axis;                                // Moment axis
    casacore::Vector<float> _include_pix;
    casacore::Vector<float> _exclude_pix;
    double _rest_frequency; // Hz
    casacore::String _error_msg;
    bool _success;
    bool _cancel;
    std::unordered_map<CARTA::Moment, ImageMoments<casacore::Float>::MomentTypes> _moment_map;
    std::unordered_map<ImageMoments<casacore::Float>::MomentTypes, casacore::String> _moment_suffix_map;

    // Progress parameters
    int _total_steps;
    float _progress;
    float _pre_progress;
    GeneratorProgressCallback _progress_callback;
    std::chrono::high_resolution_clock::time_point _start_time;
    bool _first_report_made;
};

// The chunk grid `region` of `image` lies on: the shape of a chunk, where the region's corner is among
// them, and what one chunk decodes to in a cache -- its stored type and the flag beside it, as carta-zarr
// counts it -- for ImageMoments::SetChunkGrid. False for an image that does not decode in chunks it can
// name, which is every image but a Zarr store's.
bool ChunkGridOf(const casacore::ImageInterface<float>& image, const casacore::SubImage<float>& region, casacore::IPosition& unit,
    casacore::IPosition& origin, std::uint64_t& decoded_chunk_bytes);

// A copy of `image` for a moment to read through, whose reads keep what they decode in a cache of the
// moment's own while `hold_cache` holds one -- for ImageMoments::SetChunkGrid. The walk comes back to the
// chunks its neighbouring slabs share, which the session's cache is too small to keep and would evict
// what is being looked at to try. Null, with `hold_cache` left empty, for an image that is not a Zarr
// store's.
std::shared_ptr<casacore::ImageInterface<float>> WithCacheOfItsOwn(
    const casacore::ImageInterface<float>& image, std::function<std::shared_ptr<void>(std::uint64_t bytes)>& hold_cache);

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_MOMENTGENERATOR_H_
