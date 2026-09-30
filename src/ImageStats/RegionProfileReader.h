/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_REGIONPROFILEREADER_H_
#define CARTA_SRC_IMAGESTATS_REGIONPROFILEREADER_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/aipstype.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include "ImageStats/BatchOutcome.h"
#include "Util/Image.h"

namespace carta {

// The totals of one channel of a region's profile so far: what every statistic of it is made from.
// See Totals in CONTEXT.md; the count of pixels that are not finite is kept beside them.
struct ChannelTotals {
    // Whether the reader has said anything of this channel yet. One it has not is undefined
    // throughout, its counts too.
    bool read = false;
    double num_pixels = 0.0;
    double nan_count = 0.0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
};

// Which region profile to read: a mask over a box of one plane, at `origin` in image pixels, over a
// run of channels of one stokes. The mask is borrowed for the call.
struct RegionProfileRequest {
    casacore::IPosition origin;
    const casacore::ArrayLattice<casacore::Bool>* mask = nullptr;
    AxisRange channels;
    int stokes = 0;
};

// How far a region's profile has got, kept between calls by whoever asks for it. `channels` is one
// entry per channel asked for, allocated by the caller. `done` of `total` is the reader's own measure
// of the work -- columns of the region, channels, whatever it proceeds by -- set by the reader on its
// first call and never by anyone else.
struct RegionProfileProgress {
    std::vector<ChannelTotals> channels;
    std::uint64_t done = 0;
    std::uint64_t total = 0;

    bool Complete() const {
        return total > 0 && done >= total;
    }
};

// A loader's own way of reading region profiles, a piece at a time: see Region profile in CONTEXT.md.
// Reached through FileLoader::ProfileReader(), which is null for a loader that has none.
class RegionProfileReader {
public:
    virtual ~RegionProfileReader() = default;

    // Reads the profile on from where `progress` says, into its channels, and returns once it is
    // complete or `deadline` has passed -- at a boundary of its own, and never before it has gone one
    // piece further. `finished` is having read on without trouble, however far; `progress` says how
    // far. `declined` is a request this reader does not serve, answered before anything is read, and
    // leaves `progress` alone. `report` is told the fraction done while a piece is still being read,
    // by a reader that has something to say then; returning false from it cancels.
    //
    // `image_mutex` is the frame's lock over the image, held while reading by a loader that cannot be
    // read from two threads at once.
    virtual BatchOutcome ReadOn(const RegionProfileRequest& request, std::mutex& image_mutex, RegionProfileProgress& progress,
        std::chrono::steady_clock::time_point deadline, const std::function<bool(double fraction)>& report) = 0;

    // The area of the beam flux densities are divided by, NaN when the image has none.
    virtual double BeamArea() = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_REGIONPROFILEREADER_H_
