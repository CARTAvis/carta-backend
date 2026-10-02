/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_
#define CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_

#include <optional>

#include <carta-zarr/carta_zarr.h>

namespace carta {

// How a plane of a file is read: the library's image, the options its reads go through -- and so the
// cache they keep what they decode in -- and the read of the plane itself.
struct ZarrPlaneRead {
    carta::zarr::Image image;
    carta::zarr::ReadOptions options;
    carta::zarr::ReadRequest request;
};

// What reading ahead of an animation needs of a file's loader: how each plane is read. Which run of
// chunks a plane is in, what a run holds, whether the cache has room for it and when to decode it are
// carta-zarr's ReadAhead's to say -- see AnimationReadAhead -- and need nothing more of a loader.
//
// Asked of a loader through FileLoader::ReadAhead, and offered by none but the Zarr loader's.
class PlaneReadAhead {
public:
    virtual ~PlaneReadAhead() = default;

    // How plane (z, stokes) is read, or nothing for a plane the file does not have.
    virtual std::optional<ZarrPlaneRead> Plane(int z, int stokes) const = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_
