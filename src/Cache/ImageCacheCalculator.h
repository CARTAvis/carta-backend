/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__SPECTRALPROFILES_H_
#define CARTA_BACKEND__SPECTRALPROFILES_H_

#include "Cache/CubeImageCache.h"
#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <vector>

namespace carta {

class ImageCacheCalculator {
public:
    static float GetValue(CubeImageCache& cube_image_cache, int x, int y, int z, int stokes, size_t width, size_t height);

    static bool GetPointSpectralData(CubeImageCache& cube_image_cache, std::vector<float>& profile, int stokes, PointXy point, size_t width,
        size_t height, size_t depth);

    static bool GetRegionSpectralData(CubeImageCache& cube_image_cache, const AxisRange& z_range, int stokes, size_t width, size_t height,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
        std::map<CARTA::StatsType, std::vector<double>>& profiles);
};

} // namespace carta

#endif // CARTA_BACKEND__SPECTRALPROFILES_H_
