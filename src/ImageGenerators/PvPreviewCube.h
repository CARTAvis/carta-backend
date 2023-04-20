/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_

#include "ImageGenerators/ImageGenerator.h"
#include "Region/Region.h"
#include "Util/File.h"
#include "Util/Image.h"

#include <casacore/images/Images/SubImage.h>

namespace carta {

struct PreviewCubeParameters {
    int file_id;
    int region_id;
    AxisRange spectral_range;
    int rebin_xy;
    int rebin_z;
    int stokes;
    RegionState region_state;

    PreviewCubeParameters() : file_id(-1) {}
    PreviewCubeParameters(int file_id_, int region_id_, const AxisRange& spectral_range_, int rebin_xy_, int rebin_z_, int stokes_,
        RegionState& region_state_)
        : file_id(file_id_),
          region_id(region_id_),
          spectral_range(spectral_range_),
          rebin_xy(rebin_xy_),
          rebin_z(rebin_z_),
          stokes(stokes_),
          region_state(region_state_) {}

    bool operator==(const PreviewCubeParameters& other) {
        return ((other.file_id == ALL_FILES || file_id == other.file_id) && (region_id == other.region_id) &&
                (spectral_range == other.spectral_range) && (rebin_xy == other.rebin_xy) && (rebin_z == other.rebin_z) &&
                (stokes == other.stokes) && (region_state == other.region_state));
    }
};

class PvPreviewCube {
public:
    PvPreviewCube(const PreviewCubeParameters& parameters);
    ~PvPreviewCube() = default;

    // Cube parameters
    PreviewCubeParameters parameters();
    bool HasSameParameters(const PreviewCubeParameters& parameters);
    bool HasFileId(int file_id);
    int GetStokes();

    // Filename of source image for preview image name
    void SetSourceFileName(const std::string& name);
    std::string GetSourceFileName();

    // blc of preview region's bounding box in source image
    void SetPreviewRegionOrigin(const casacore::IPosition& origin);

    // Return cached preview image and cache cube data if not done previously
    std::shared_ptr<casacore::ImageInterface<float>> GetPreviewImage(
        GeneratorProgressCallback progress_callback, bool& cancel, std::string& message);

    // Create preview image by applying rebinning to SubImage, and cache cube data.
    std::shared_ptr<casacore::ImageInterface<float>> GetPreviewImage(
        casacore::SubImage<float>& sub_image, GeneratorProgressCallback progress_callback, bool& cancel, std::string& message);

    // Set PV cut in preview cube
    RegionState GetPvCutRegion(const RegionState& source_region_state, int preview_frame_id);

    // Apply region and mask to preview cube for spectral profile and maximum number of per-channel pixels.
    // Include progress callback in case data must be loaded
    bool GetRegionProfile(const casacore::Slicer& region_bounding_box, const casacore::ArrayLattice<casacore::Bool>& mask,
        GeneratorProgressCallback progress_callback, std::vector<float>& profile, double& num_pixels, bool& cancel, std::string& message);

    // Cancel preview image and cube data cache.
    void StopCube();

private:
    // Rebin cube parameters > 1
    bool DoRebin();

    // Cache cube data for preview image
    void LoadCubeData(GeneratorProgressCallback progress_callback, bool& cancel);
    bool CubeLoaded();

    // Cube parameters
    PreviewCubeParameters _cube_parameters;

    // Name of source image
    std::string _source_filename;

    // Origin (blc) of preview region in source image
    casacore::IPosition _origin;

    // Preview image cube: SubImage with downsampling applied if rebin
    std::shared_ptr<casacore::ImageInterface<float>> _preview_image;

    // SubImage for data access when preview image is RebinImage
    casacore::SubImage<float> _preview_subimage;

    // Image cube cache
    casacore::Array<float> _cube_data;

    // Flag to stop caching image cube
    bool _stop_cube;
    std::string _cancel_message;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
