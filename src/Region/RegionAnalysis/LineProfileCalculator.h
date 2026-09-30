/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_LINEPROFILECALCULATOR_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_LINEPROFILECALCULATOR_H_

#include <cstddef>
#include <functional>
#include <vector>

#include <casacore/casa/Arrays/Matrix.h>

#include "ImageStats/BatchOutcome.h"

namespace carta {

// Some of the line profiles, as a route hands them over: `box_count` boxes from `first_box`, over
// `channel_count` channels from `first_channel`. Channels count from the first one asked for, not from
// the first of the image.
//
// A loader's walk hands over a run of channels of every box at once, and the boxes taken one at a
// time hand over every channel of one box; both are a block. Valid only for the call it arrives with.
struct LineProfileBlock {
    std::size_t first_box = 0;
    std::size_t box_count = 0;
    std::size_t first_channel = 0;
    std::size_t channel_count = 0;
    // The mean of box `first_box + box` in channel `first_channel + channel`, NaN where the box
    // caught no valid pixel.
    std::function<float(std::size_t box, std::size_t channel)> mean;
    // Whether these means are final. A walk whose block spans more than one read hands it over as it
    // fills, and then a last time complete; `completeness` is the fraction of it read so far.
    bool complete = true;
    double completeness = 1.0;
};

// One way of making line profiles. See Route in CONTEXT.md. The boxes, the channels and the stokes are
// the route's own, given when it is made.
//
// Returning false from `sink` cancels. `cancellation_requested` is asked by a route with a reason to
// stop between blocks of its own, and may be empty. Declining is as BatchOutcome says: before anything
// is read and before `sink` is called.
class BoxReducer {
public:
    virtual ~BoxReducer() = default;

    virtual BatchOutcome BoxMeans(
        const std::function<bool()>& cancellation_requested, const std::function<bool(const LineProfileBlock&)>& sink) = 0;
};

// A route, named for the log. The reducer is never null and must outlive the calculation.
struct LineRoute {
    const char* name = "";
    BoxReducer* reducer = nullptr;
};

// How a calculation ended. Declined is not a case, as for a cube histogram: the calculator takes the
// next route after it. `failed` is every route declining, a route that began and did not finish -- not
// retried, since the next reads the same pixels -- or nothing to make.
enum class LineProfileOutcome { finished, cancelled, failed };

// How a calculation is watched and stopped. Either may be empty.
struct LineProfileControl {
    // Asked before each block is taken, and handed to the route to ask between its own.
    std::function<bool()> cancellation_requested;
    // Told the fraction of the profiles made, after every block. A caller that reports on a timer
    // throttles this itself. The last report of a finished calculation is one.
    std::function<void(float progress)> progress;
};

// Makes the line profiles from whichever routes will answer: every box's mean over every channel.
// See Line profiles in CONTEXT.md.
//
// The routes are asked in the order given, a walk first and the boxes one at a time last, and the
// first that does not decline makes all of them. What it knows nothing of is where the boxes are, or
// what a PV image does with an answer that is all NaN.
class LineProfileCalculator {
public:
    explicit LineProfileCalculator(std::vector<LineRoute> routes);

    // `profiles` is written only when the outcome is finished: `boxes` rows of `channels` means, or the
    // other way round when `reverse` is set, NaN wherever no route said otherwise.
    LineProfileOutcome Calculate(
        std::size_t boxes, std::size_t channels, bool reverse, const LineProfileControl& control, casacore::Matrix<float>& profiles) const;

private:
    std::vector<LineRoute> _routes;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_LINEPROFILECALCULATOR_H_
