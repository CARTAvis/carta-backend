/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_
#define CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_

#include <cstdint>
#include <functional>

namespace carta {

// Which chunks a plane decodes: the chunk along the channels and the chunk along the Stokes it lies in.
// Two planes in one run decode the same chunks, so reading one leaves the other decoded.
struct PlaneRun {
    int z = 0;
    int stokes = 0;

    bool operator==(const PlaneRun& other) const {
        return z == other.z && stokes == other.stokes;
    }
    bool operator!=(const PlaneRun& other) const {
        return !(*this == other);
    }
};

// Decoding a plane's chunks before anyone asks for the plane, for a loader whose reads keep what they
// decode in a cache the next read finds. An animation is the reader that knows what it will ask for:
// the frame that enters a new run of chunks decodes all of it, and that is the frame that stalls,
// unless the run was decoded while the frames before it played from the one before.
//
// Asked of a loader through FileLoader::ReadAhead, and offered by none but the Zarr loader's.
class PlaneReadAhead {
public:
    virtual ~PlaneReadAhead() = default;

    // The run of chunks plane (z, stokes) is in.
    virtual PlaneRun RunOf(int z, int stokes) const = 0;
    // What one run holds once decoded, in bytes: the plane, rounded out to whole chunks, times a chunk's
    // depth along the channels and the Stokes.
    virtual std::uint64_t RunBytes() const = 0;
    // How much the cache that runs are decoded into holds, in bytes.
    virtual std::uint64_t CacheBytes() const = 0;
    // Decodes the run plane (z, stokes) is in, so that reading any plane of it finds its chunks
    // decoded. Blocks until it has, or `cancelled` says to stop, or it fails, and says whether it
    // finished. Safe to call while planes are being read, and takes no lock a read of a plane takes:
    // a frame that needs the run before it is decoded waits for the decoding under way rather than
    // starting its own.
    virtual bool Prefetch(int z, int stokes, const std::function<bool()>& cancelled) = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_PLANEREADAHEAD_H_
