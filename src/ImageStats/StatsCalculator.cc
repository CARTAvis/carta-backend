/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# StatsCalculator.cc: functions for calculating statistics and histograms

#include "StatsCalculator.h"

#include <cmath>
#include <limits>

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/images/Images/ImageStatistics.h>

namespace carta {

void CalcBasicStats(BasicStats<float>& stats, const float* data, const size_t data_size) {
    // Calculate stats in BasicStats struct
    BasicStatsCalculator<float> mm(data, data_size);
    mm.reduce();
    stats = mm.GetStats();
}

Histogram CalcHistogram(int num_bins, const HistogramBounds& bounds, const float* data, const size_t data_size) {
    if (bounds.Invalid<float>() || data_size == 0) {
        // empty / NaN region
        return Histogram(1, HistogramBounds(0, 0), data, data_size);
    }
    return Histogram(num_bins, bounds, data, data_size);
}

bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<CARTA::StatsType>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel) {
    // Use ImageStatistics to fill statistics values according to type;
    // template type matches image type
    casacore::ImageStatistics<float> image_stats = casacore::ImageStatistics<float>(image,
        /*showProgress*/ false, /*forceDisk*/ false, /*clone*/ false);

    size_t result_size(1); // expected size of returned vector per stat
    if (per_channel) {     // get stats per xy plane
        casacore::Vector<int> display_axes(2);
        display_axes(0) = 0;
        display_axes(1) = 1;
        if (!image_stats.setAxes(display_axes)) {
            return false;
        }

        casacore::IPosition xy_axes(display_axes);
        result_size = image.shape().removeAxes(xy_axes).product();
    }

    casacore::Array<casacore::Double> num_points;
    size_t num_stats(requested_stats.size());
    for (size_t i = 0; i < num_stats; ++i) {
        // get requested statistics values
        casacore::LatticeStatsBase::StatisticsTypes lattice_stats_type(casacore::LatticeStatsBase::NSTATS);

        std::vector<double> dbl_result; // lattice stats
        std::vector<int> int_result;    // position stats
        auto carta_stats_type = requested_stats[i];

        switch (carta_stats_type) {
            case CARTA::StatsType::NumPixels:
                lattice_stats_type = casacore::LatticeStatsBase::NPTS;
                break;
            case CARTA::StatsType::Sum:
                lattice_stats_type = casacore::LatticeStatsBase::SUM;
                break;
            case CARTA::StatsType::FluxDensity:
                lattice_stats_type = casacore::LatticeStatsBase::FLUX;
                break;
            case CARTA::StatsType::Mean:
                lattice_stats_type = casacore::LatticeStatsBase::MEAN;
                break;
            case CARTA::StatsType::RMS:
                lattice_stats_type = casacore::LatticeStatsBase::RMS;
                break;
            case CARTA::StatsType::Sigma:
                lattice_stats_type = casacore::LatticeStatsBase::SIGMA;
                break;
            case CARTA::StatsType::SumSq:
                lattice_stats_type = casacore::LatticeStatsBase::SUMSQ;
                break;
            case CARTA::StatsType::Min:
            case CARTA::StatsType::Extrema:
                lattice_stats_type = casacore::LatticeStatsBase::MIN;
                break;
            case CARTA::StatsType::Max:
                lattice_stats_type = casacore::LatticeStatsBase::MAX;
                break;
            case CARTA::StatsType::Blc: {
                const casacore::IPosition blc(image.region().slicer().start());
                int_result = blc.asStdVector();
                break;
            }
            case CARTA::StatsType::Trc: {
                const casacore::IPosition trc(image.region().slicer().end());
                int_result = trc.asStdVector();
                break;
            }
            case CARTA::StatsType::MinPos:
            case CARTA::StatsType::MaxPos: {
                if (!per_channel) { // only works when no display axes
                    const casacore::IPosition blc(image.region().slicer().start());
                    casacore::IPosition min_pos, max_pos;
                    image_stats.getMinMaxPos(min_pos, max_pos);
                    if (carta_stats_type == CARTA::StatsType::MinPos)
                        int_result = (blc + min_pos).asStdVector();
                    else // MaxPos
                        int_result = (blc + max_pos).asStdVector();
                }
                break;
            }
            default:
                break;
        }

        if (lattice_stats_type < casacore::LatticeStatsBase::NSTATS) {
            casacore::Array<casacore::Double> result;
            try {
                if (image_stats.getStatistic(result, lattice_stats_type)) {
                    // return result Array for stats type
                    if (anyEQ(result, 0.0)) {
                        // Convert 0 result to NaN if number of points is zero
                        if (num_points.empty()) {
                            image_stats.getStatistic(num_points, casacore::LatticeStatsBase::NPTS);
                        }

                        for (size_t j = 0; j < result.size(); ++j) {
                            casacore::IPosition index(1, j);
                            if ((result(index) == 0.0) && (num_points(index) == 0.0)) {
                                result(index) = nan("");
                            }
                        }
                    }

                    if (carta_stats_type == CARTA::StatsType::Extrema) {
                        std::vector<double> min_result;
                        result.tovector(min_result);

                        if (image_stats.getStatistic(result, casacore::LatticeStatsBase::MAX)) {
                            std::vector<double> max_result;
                            result.tovector(max_result);
                            std::transform(min_result.begin(), min_result.end(), max_result.begin(), std::back_inserter(dbl_result),
                                [](double min, double max) { return (abs(min) > abs(max) ? min : max); });
                        }
                    } else {
                        result.tovector(dbl_result);
                    }
                }
            } catch (const casacore::AipsError& err) {
                // Catch exception for calculated stat, e.g. flux density; set result to NaN
                for (size_t j = 0; j < result_size; ++j) {
                    dbl_result.push_back(nan(""));
                }
            }
        }

        if (!int_result.empty()) {
            dbl_result.reserve(int_result.size());
            for (unsigned int j = 0; j < int_result.size(); ++j) { // convert to double
                dbl_result.push_back(static_cast<double>(int_result[j]));
            }
        }

        if (!dbl_result.empty()) {
            stats_values.emplace(carta_stats_type, dbl_result);
        }
    }

    return true;
}
} // namespace carta
