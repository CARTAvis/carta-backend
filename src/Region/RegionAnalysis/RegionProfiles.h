/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_REGION_REGIONANALYSIS_REGIONPROFILES_H_
#define CARTA_SRC_REGION_REGIONANALYSIS_REGIONPROFILES_H_

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <carta-protobuf/defs.pb.h>

#include "ImageStats/RegionProfileReader.h"

namespace carta {

// Whose profile: a region of a file, in one stokes.
struct RegionProfileKey {
    int file_id = 0;
    int region_id = 0;
    int stokes = 0;

    bool operator<(const RegionProfileKey& other) const {
        if (file_id != other.file_id) {
            return file_id < other.file_id;
        }
        if (region_id != other.region_id) {
            return region_id < other.region_id;
        }
        return stokes < other.stokes;
    }
};

// Told how far a step has got while it is still reading: the fraction done, and the profile so far,
// which is made only if asked for. Returning false cancels.
using RegionProfileReport =
    std::function<bool(float progress, const std::function<std::map<CARTA::StatsType, std::vector<double>>()>& partial)>;

// The region profiles being made, and those made: see Region profile in CONTEXT.md.
//
// A profile is made a step at a time, and between steps the caller decides whether it is still
// wanted; each step resumes where the last left off. It resumes only a request for the same box, mask
// extent and channels: any other is a different question, and starts again. What a region's profile
// is kept against says nothing of the mask's contents, so an edit that keeps the box is not seen here,
// and the caller releases the region when it is edited or removed. A finished profile is kept, and
// asking again is answered from it.
//
// The statistics are made here from the totals the reader keeps, whichever reader it was.
class RegionProfiles {
public:
    // One step on, of at most `step` if the reader can stop that soon. When the outcome is finished,
    // `profile` is the profile so far and `progress` the fraction of it that is done, one when it is
    // complete. Declined is the reader's, and then nothing is kept. A profile cancelled or failed
    // partway is kept as far as it got.
    BatchOutcome Continue(const RegionProfileKey& key, const RegionProfileRequest& request, RegionProfileReader& reader,
        std::mutex& image_mutex, std::chrono::milliseconds step, const RegionProfileReport& report,
        std::map<CARTA::StatsType, std::vector<double>>& profile, float& progress);

    // Lets a region's profiles go, of every file and stokes, or every region's with ALL_REGIONS.
    void Release(int region_id);
    // Lets every profile of a file go.
    void ReleaseFile(int file_id);

    // How many profiles are kept, finished or not.
    std::size_t Size() const;

private:
    struct Entry {
        std::mutex mutex;
        casacore::IPosition origin;
        casacore::IPosition shape;
        AxisRange channels;
        RegionProfileProgress progress;
    };

    // Guards the map and nothing in it, and is never held across a step: a step holds its entry
    // through the pointer, so a region released while its step runs leaves the step to finish into an
    // entry nobody will ask for again.
    mutable std::mutex _mutex;
    std::map<RegionProfileKey, std::shared_ptr<Entry>> _entries;
};

// The statistics CARTA reports of a region profile, from its channels' totals and the beam area: see
// Derived statistics in CONTEXT.md. A channel not yet read is NaN throughout; one read with no valid
// pixel is NaN but for its two counts.
std::map<CARTA::StatsType, std::vector<double>> RegionProfileStatistics(const std::vector<ChannelTotals>& channels, double beam_area);

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONANALYSIS_REGIONPROFILES_H_
