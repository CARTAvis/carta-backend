/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_FILEINFO_H_
#define CARTA_SRC_IMAGEDATA_FILEINFO_H_

#include <map>
#include <mutex>
#include <vector>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>

#include <carta-protobuf/defs.pb.h>

namespace carta {
namespace FileInfo {

struct ImageStats {
    std::map<CARTA::StatsType, double> basic_stats;

    std::vector<float> percentiles;
    std::vector<float> percentile_ranks;
    std::vector<int> histogram_bins;

    bool valid = false;
    // Remove this check when we drop support for the old schema.
    bool full;
};

struct RegionStatsId {
    int region_id;
    int stokes;

    RegionStatsId() {}

    RegionStatsId(int region_id, int stokes) : region_id(region_id), stokes(stokes) {}

    bool operator<(const RegionStatsId& rhs) const {
        return (region_id < rhs.region_id) || ((region_id == rhs.region_id) && (stokes < rhs.stokes));
    }
};

// What a region's spectral profile has accumulated so far, for the loader that resumes it. It belongs
// to one box and mask extent over one run of channels, and to nothing else.
struct RegionSpectralStats {
    casacore::IPosition origin;
    casacore::IPosition shape;
    int z_from = 0;
    int z_to = 0;
    std::map<CARTA::StatsType, std::vector<double>> stats;
    volatile bool completed = false;
    size_t latest_x = 0;
    // Held for the whole of one call, so that two calls for the same region and stokes take turns.
    std::mutex mutex;

    RegionSpectralStats(casacore::IPosition origin, casacore::IPosition shape, int z_from, int z_to, bool has_flux = false)
        : origin(origin), shape(shape), z_from(z_from), z_to(z_to) {
        const int num_channels = z_to - z_from + 1;
        std::vector<CARTA::StatsType> supported_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::NanCount, CARTA::StatsType::Sum,
            CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min,
            CARTA::StatsType::Max, CARTA::StatsType::Extrema};

        if (has_flux) {
            supported_stats.push_back(CARTA::StatsType::FluxDensity);
        }

        for (auto& s : supported_stats) {
            stats.emplace(std::piecewise_construct, std::make_tuple(s), std::make_tuple(num_channels));
        }
    }

    // Whether a request is a continuation of this one. A different box, mask extent or run of channels
    // is a different question, whose answer would otherwise be resumed from this one's columns.
    bool IsValid(const casacore::IPosition& origin, const casacore::IPosition& shape, int z_from, int z_to) const {
        return origin.isEqual(this->origin) && shape.isEqual(this->shape) && z_from == this->z_from && z_to == this->z_to;
    }
    bool IsCompleted() {
        return completed;
    }
};

enum class Data : uint32_t {
    // Main dataset
    Image,
    // Possible aliases to main dataset
    XY,
    XYZ,
    XYZW,
    // Possible swizzled datasets
    YX,
    ZYX,
    ZYXW,
    // Alias to swizzled dataset
    SWIZZLED,
    // Statistics tables
    STATS,
    RANKS,
    STATS_2D,
    STATS_2D_MIN,
    STATS_2D_MAX,
    STATS_2D_SUM,
    STATS_2D_SUMSQ,
    STATS_2D_NANS,
    STATS_2D_HIST,
    STATS_2D_PERCENT,
    STATS_3D,
    STATS_3D_MIN,
    STATS_3D_MAX,
    STATS_3D_SUM,
    STATS_3D_SUMSQ,
    STATS_3D_NANS,
    STATS_3D_HIST,
    STATS_3D_PERCENT,
    // Mask
    MASK
};

inline casacore::uInt GetFitsHdu(const std::string& hdu) {
    // convert from string to casacore unsigned int
    casacore::uInt hdu_num(0);
    if (!hdu.empty() && hdu != "0") {
        casacore::String cc_hdu(hdu);
        cc_hdu.fromString(hdu_num, true);
    }
    return hdu_num;
}

} // namespace FileInfo
} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_FILEINFO_H_
