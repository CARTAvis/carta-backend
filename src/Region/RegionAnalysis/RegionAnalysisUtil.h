/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionAnalysisUtil.h: Common utilities for region analysis

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_REGIONANALYSISUTIL_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_REGIONANALYSISUTIL_H_

#include "Frame/Frame.h"

namespace carta {

/**
 * @brief Get stokes slicer for region, z range, and stokes applied to image.
 * @param frame Image frame
 * @param lcregion Region applied to image
 * @param stokes_source Struct describing stokes and z range
 * @return StokesSlicer for region
 */
StokesSlicer GetRegionStokesSlicer(
    std::shared_ptr<Frame> frame, std::shared_ptr<casacore::LCRegion> lcregion, StokesSource& stokes_source) {
    StokesSlicer image_slicer = frame->GetImageSlicer(stokes_source.z_range, stokes_source.stokes);
    auto start = image_slicer.slicer.start();
    auto end = image_slicer.slicer.end();

    // Set start and end to region slicer
    auto region_slicer = lcregion->boundingBox();
    start[0] = region_slicer.start()[0];
    start[1] = region_slicer.start()[1];
    end[0] = region_slicer.end()[0];
    end[1] = region_slicer.end()[1];
    casacore::Slicer stokes_slicer(start, end, casacore::Slicer::endIsLast);
    return StokesSlicer(stokes_source, stokes_slicer);
}

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_REGIONANALYSISUTIL_H_
