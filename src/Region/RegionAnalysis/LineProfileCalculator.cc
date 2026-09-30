/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "LineProfileCalculator.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "Util/Nan.h"

namespace carta {

namespace {

// Writes a block's means where they belong: a box to a row, or to a column when reversed.
void Lay(casacore::Matrix<float>& profiles, const LineProfileBlock& block, bool reverse) {
    for (std::size_t box = 0; box < block.box_count; ++box) {
        for (std::size_t channel = 0; channel < block.channel_count; ++channel) {
            const auto row = block.first_box + box;
            const auto column = block.first_channel + channel;
            if (reverse) {
                profiles(column, row) = block.mean(box, channel);
            } else {
                profiles(row, column) = block.mean(box, channel);
            }
        }
    }
}

} // namespace

LineProfileCalculator::LineProfileCalculator(std::vector<LineRoute> routes) : _routes(std::move(routes)) {}

LineProfileOutcome LineProfileCalculator::Calculate(
    std::size_t boxes, std::size_t channels, bool reverse, const LineProfileControl& control, casacore::Matrix<float>& profiles) const {
    if (boxes == 0 || channels == 0) {
        return LineProfileOutcome::failed;
    }
    const auto stopped = [&]() { return control.cancellation_requested && control.cancellation_requested(); };
    float reported = 0.0f;
    const auto report = [&](float progress) {
        reported = progress;
        if (control.progress) {
            control.progress(progress);
        }
    };

    casacore::Matrix<float> made;
    if (reverse) {
        made.resize(casacore::IPosition(2, channels, boxes));
    } else {
        made.resize(casacore::IPosition(2, boxes, channels));
    }
    made = FLOAT_NAN;

    // Progress is the share of every box's every channel that is final, and a block still filling
    // counts for the part of it that has been read. A walk's blocks are runs of channels and the
    // boxes one at a time are boxes, and this is the same fraction of either.
    const double cells = static_cast<double>(boxes) * static_cast<double>(channels);
    double cells_done = 0.0;

    bool out_of_bounds = false;
    const auto take = [&](const LineProfileBlock& block) {
        if (stopped()) {
            return false;
        }
        if (block.first_box + block.box_count > boxes || block.first_channel + block.channel_count > channels) {
            out_of_bounds = true;
            return false;
        }
        Lay(made, block, reverse);

        const double block_cells = static_cast<double>(block.box_count) * static_cast<double>(block.channel_count);
        double done = cells_done;
        if (block.complete) {
            cells_done += block_cells;
            done = cells_done;
        } else {
            done += block.completeness * block_cells;
        }
        report(static_cast<float>(done / cells));
        return true;
    };

    for (const auto& route : _routes) {
        const auto outcome = route.reducer->BoxMeans(control.cancellation_requested, take);
        if (outcome == BatchOutcome::declined) {
            continue;
        }
        spdlog::debug("Line profiles: taken by the {} route", route.name);
        if (out_of_bounds) {
            spdlog::error("Line profiles: the {} route handed over a block outside the profiles", route.name);
            return LineProfileOutcome::failed;
        }
        if (outcome == BatchOutcome::cancelled) {
            return LineProfileOutcome::cancelled;
        }
        if (outcome == BatchOutcome::failed) {
            return LineProfileOutcome::failed;
        }
        if (reported < 1.0f) {
            report(1.0f);
        }
        profiles.reference(made);
        return LineProfileOutcome::finished;
    }
    return LineProfileOutcome::failed;
}

} // namespace carta
