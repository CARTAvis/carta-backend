/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARR_PLANEREADAHEAD_H_
#define CARTA_SRC_IMAGEDATA_ZARR_PLANEREADAHEAD_H_

#include <optional>

#include <carta-zarr/carta_zarr.h>

namespace carta {

// How a plane of a file is read: the carta-zarr image, the read options (and so the cache the read uses),
// and the read request.
struct ZarrPlaneRead {
    carta::zarr::Image image;
    carta::zarr::ReadOptions options;
    carta::zarr::ReadRequest request;
};

// What reading ahead of an animation needs from a loader: how each plane is read.
// carta-zarr's ReadAhead decides which chunks to decode ahead and when. See AnimationReadAhead.
// Only the Zarr loader has one. FileLoader::ReadAhead returns it.
class PlaneReadAhead {
public:
    virtual ~PlaneReadAhead() = default;

    // How plane (z, stokes) is read. Returns nothing for a plane that the file does not have.
    virtual std::optional<ZarrPlaneRead> Plane(int z, int stokes) const = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARR_PLANEREADAHEAD_H_
