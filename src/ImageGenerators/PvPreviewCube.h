/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_

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

    PreviewCubeParameters() : file_id(-1) {}
    PreviewCubeParameters(int file_id_, int region_id_, const AxisRange& spectral_range_, int rebin_xy_, int rebin_z_, int stokes_)
        : file_id(file_id_),
          region_id(region_id_),
          spectral_range(spectral_range_),
          rebin_xy(rebin_xy_),
          rebin_z(rebin_z_),
          stokes(stokes_) {}

    bool operator==(const PreviewCubeParameters& other) {
        return ((other.file_id == ALL_FILES || file_id == other.file_id) && (region_id == other.region_id) &&
                (spectral_range == other.spectral_range) && (rebin_xy == other.rebin_xy) && (rebin_z == other.rebin_z) &&
                (stokes == other.stokes));
    }

    bool UseFullImage(int num_channels) {
        // true if image region, full spectral range, and no rebin
        return (region_id == IMAGE_REGION_ID) && (spectral_range.from == 0) && (spectral_range.to == num_channels - 1) && (rebin_xy == 0) &&
               (rebin_z == 0);
    }
};

class PvPreviewCube {
public:
    PvPreviewCube(int file_id_, int region_id_, const AxisRange& spectral_range_, int rebin_xy_, int rebin_z_, int stokes_);
    PvPreviewCube(const PreviewCubeParameters& parameters);

    // Cube parameters
    bool HasSameParameters(const PreviewCubeParameters& parameters);
    bool UseFullImage(int num_channels);

    // blc of preview region's bounding box in source image
    void SetPreviewRegionOrigin(const casacore::IPosition& origin);
    casacore::IPosition PreviewRegionOrigin();

    // Return stored preview image (or nullptr)
    std::shared_ptr<casacore::ImageInterface<float>> GetPreviewImage();

    // Create preview image by applying rebinning to SubImage, and store it.
    // Input SubImage is preview region and spectral range applied to source image.
    std::shared_ptr<casacore::ImageInterface<float>> GetPreviewImage(casacore::SubImage<float>& sub_image);

    void StopCube();

private:
    // Cube parameters
    PreviewCubeParameters _cube_parameters;

    // Origin (blc) of preview region in source image
    casacore::IPosition _origin;

    // Preview image cube: SubImage with downsampling applied
    std::shared_ptr<casacore::ImageInterface<float>> _preview_image;

    // Flag to stop downsampling image
    bool _stop_cube;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
