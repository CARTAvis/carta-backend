/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// ImageRegion.h: functions to apply region to image

#ifndef CARTA_SRC_REGION_IMAGEREGION_H_
#define CARTA_SRC_REGION_IMAGEREGION_H_

#include "Frame/Frame.h"

namespace carta {

/**
 * @brief Apply Region to image in Frame in image coordinates.
 * @param[in] region Region object to apply
 * @param[in] frame Image frame to apply region to
 * @param[in] stokes_source Struct describing stokes and z range
 * @param[in] lc_region Region in lattice coordinates
 * @param[out] image_region Region in image coordinates
 * @return Whether ImageRegion was created
 */
bool GetImageRegion(std::shared_ptr<Region> region, std::shared_ptr<Frame> frame, const AxisRange& z_range, int stokes,
    std::shared_ptr<casacore::LCRegion> lc_region, casacore::ImageRegion& image_region);

} // namespace carta

#endif // CARTA_SRC_REGION_IMAGEREGION_H_
