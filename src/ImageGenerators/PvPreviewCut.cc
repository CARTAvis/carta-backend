/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvPreviewCut.h"

#include "DataStream/Compression.h"

namespace carta {

PvPreviewCut::PvPreviewCut(const PreviewCutParameters& parameters, const RegionState& region_state) : _cut_parameters(parameters) {
    AddRegion(region_state);
}

PvPreviewCut::~PvPreviewCut() {
    ClearRegionQueue();
}

bool PvPreviewCut::HasSameParameters(const PreviewCutParameters& parameters) {
    return _cut_parameters == parameters;
}

bool PvPreviewCut::HasFileRegionIds(int file_id, int region_id) {
    // Check if file and region ids are for this cut
    return _cut_parameters.HasFileRegionIds(file_id, region_id);
}

int PvPreviewCut::GetWidth() {
    return _cut_parameters.width;
}

int PvPreviewCut::GetReverse() {
    return _cut_parameters.reverse;
}

bool PvPreviewCut::HasQueuedRegion() {
    std::lock_guard<std::mutex> guard(_preview_region_mutex);
    return (!_preview_region_states.empty());
}

void PvPreviewCut::AddRegion(const RegionState& region_state) {
    std::lock_guard<std::mutex> guard(_preview_region_mutex);
    _preview_region_states.push(region_state);
}

bool PvPreviewCut::GetNextRegion(RegionState& region_state) {
    // Remove and return next region state in queue.
    // Returns false if no more regions.
    std::lock_guard<std::mutex> guard(_preview_region_mutex);
    if (_preview_region_states.empty()) {
        return false;
    }
    region_state = _preview_region_states.front();
    _preview_region_states.pop();
    return true;
}

void PvPreviewCut::ClearRegionQueue() {
    std::lock_guard<std::mutex> guard(_preview_region_mutex);
    while (!_preview_region_states.empty()) {
        _preview_region_states.pop();
    }
}

bool PvPreviewCut::FillCompressedPreviewData(
    CARTA::PvPreviewData* preview_data, std::vector<float>& image_data, int width, int height, bool decrease_quality) {
    // Compress data with given shape and fill PvPreviewData message. Decrease quality setting if flag set.
    // Returns false if compression type unsupported.
    auto compression_type = _cut_parameters.compression;
    int quality = lround(_cut_parameters.quality);

    if (compression_type == CARTA::CompressionType::NONE) {
        // Complete message
        preview_data->set_image_data(image_data.data(), image_data.size() * sizeof(float));
        preview_data->set_compression_type(compression_type);
        preview_data->set_compression_quality(quality);
        return true;
    } else if (compression_type == CARTA::CompressionType::ZFP) {
        // Encode NaN values
        auto nan_encodings = GetNanEncodingsBlock(image_data, 0, width, height);

        // Compress preview image data
        std::vector<char> compression_buffer;
        size_t compressed_size;
        if (decrease_quality) {
            quality -= 2;
        }
        Compress(image_data, 0, compression_buffer, compressed_size, width, height, quality);

        // Complete message
        preview_data->set_image_data(compression_buffer.data(), compressed_size);
        preview_data->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());
        preview_data->set_compression_type(compression_type);
        preview_data->set_compression_quality(quality);
        return true;
    }

    return false; // unsupported compression
}

} // namespace carta
