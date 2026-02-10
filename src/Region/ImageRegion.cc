/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// ImageRegion.cc: functions to apply region to image

#include "ImageRegion.h"

#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>

#include "Logger/Logger.h"

namespace carta {

bool GetImageRegion(std::shared_ptr<Region> region, std::shared_ptr<Frame> frame, const AxisRange& z_range, int stokes_index,
    std::shared_ptr<casacore::LCRegion> lc_region, casacore::ImageRegion& image_region) {
    if (!lc_region) {
        return false;
    }

    try {
        // Create LCBox for z range and stokes using a slicer
        StokesSource stokes_source(stokes_index, z_range);
        casacore::IPosition image_shape(frame->ImageShape(stokes_source));
        casacore::Slicer z_stokes_slicer = frame->GetImageSlicer(z_range, stokes_index).slicer;

        // Combine LCRegion with LCBox
        if (lc_region->shape().size() == image_shape.size()) {
            // Intersection combines applied_region xy limits and box z/stokes limits
            casacore::LCBox z_stokes_box(z_stokes_slicer, image_shape);
            casacore::LCIntersection final_region(*lc_region, z_stokes_box);
            image_region = casacore::ImageRegion(final_region);
        } else {
            // Extension extends applied_region in xy axes by z/stokes axes only
            // Remove xy axes from z/stokes box
            casacore::IPosition remove_xy(2, frame->XAxis(), frame->YAxis());
            z_stokes_slicer =
                casacore::Slicer(z_stokes_slicer.start().removeAxes(remove_xy), z_stokes_slicer.length().removeAxes(remove_xy));
            casacore::LCBox z_stokes_box(z_stokes_slicer, image_shape.removeAxes(remove_xy));
            casacore::IPosition extend_axes = casacore::IPosition::makeAxisPath(image_shape.size()).removeAxes(remove_xy);
            casacore::LCExtension final_region(*lc_region, extend_axes, z_stokes_box);
            image_region = casacore::ImageRegion(final_region);
        }
        return true;
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error applying region to image: {}", err.getMesg());
    }

    return false;
}

} // namespace carta
