/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # StatsCalculator.h: functions for calculating statistics and histograms

#ifndef CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_
#define CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_

#include <vector>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageStatistics.h>

#include <carta-protobuf/enums.pb.h>
#include "BasicStatsCalculator.h"
#include "Histogram.h"

namespace carta {

/** @brief Functions to calculate image and region statistics and histograms
 *  @details These function are used to calculate basic statistics for histograms,
 *  per-channel image and region statistics for Z profiles, and per-plane image and region statistics.
 *  @see BasicStatsCalculator
 *  @see Histogram
 */

/** @brief This map is used to convert the CARTA StatsType enum to the casacore StatisticsTypes enum */
static std::unordered_map<CARTA::StatsType, casacore::LatticeStatsBase::StatisticsTypes> carta_stats_to_casacore{
    {CARTA::StatsType::Sum, casacore::LatticeStatsBase::SUM}, {CARTA::StatsType::Mean, casacore::LatticeStatsBase::MEAN},
    {CARTA::StatsType::RMS, casacore::LatticeStatsBase::RMS}, {CARTA::StatsType::Sigma, casacore::LatticeStatsBase::SIGMA},
    {CARTA::StatsType::SumSq, casacore::LatticeStatsBase::SUMSQ}, {CARTA::StatsType::Min, casacore::LatticeStatsBase::MIN},
    {CARTA::StatsType::Extrema, casacore::LatticeStatsBase::MIN}, {CARTA::StatsType::Max, casacore::LatticeStatsBase::MAX}};

/** @brief Calculate basic stats from a float vector using BasicStatsCalculator
 *  @param stats The results returned in a BasicStats struct
 *  @param data The pointer to the float vector
 *  @param data_size The size of the float vector
 */
void CalcBasicStats(BasicStats<float>& stats, const float* data, const size_t data_size);

/** @brief Calculate a histogram from a float vector using Histogram
 *  @param num_bins The number of bins for the histogram
 *  @param bounds The min and max for the histogram in a HistogramBounds struct
 *  @param data The pointer to the float vector
 *  @param data_size The size of the float vector
 *  @return The histogram
 */
Histogram CalcHistogram(int num_bins, const HistogramBounds& bounds, const float* data, const size_t data_size);

/** @brief Calculate statistics for an image or a region applied to an image
 *  @param stats_values Map to return results by StatsType
 *  @param requested_stats Vector of statistics type to calculate
 *  @param image The image or region applied to an image (casacore::SubImage)
 *  @param per_channel Whether to calculate statistics per channel plane or single stats for the entire image
 *  @return Whether the calculation succeeded
 *  @details Some statistics are calculated using the functions below, while others use casacore::ImageStatistics.
 *  @see GetPositionStats
 *  @see ComputeFluxDensity
 */
bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<CARTA::StatsType>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel = true);

/** @brief Calculate statistics related to the image/region position
 *  @param image The image or region applied to an image (casacore::SubImage)
 *  @param image_stats The casacore::ImageStatistics object for the image
 *  @param carta_stats_type The position statistics type to calculate
 *  @param dbl_result Double vector to return the statistics result
 *  @details The casacore::Slicer defining the image bounds is used to determine Blc and Trc.
 *  ImageStatistics is used to retrieve the minimum and maximum position.
 *  @see CalcStatsValues
 */
void GetPositionStats(const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats,
    CARTA::StatsType carta_stats_type, std::vector<double>& dbl_result);

/** @brief Calculate flux density statistics for an image or a region applied to an image
 *  @param image The image or region applied to an image (casacore::SubImage)
 *  @param image_stats The casacore::ImageStatistics object for the image
 *  @param result Double vector to return the flux density result
 *  @return Whether the calculation succeeded
 *  @details Flux density calculation fails when image unit is not compatible or when the unit is per beam but no beam is defined.
 *  ImageStatistics is used to retrieve the sum. GetBeamArea is called when the image unit is per beam.
 *  @see CalcStatsValues
 *  @see GetBeamArea
 */
bool ComputeFluxDensity(
    const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats, std::vector<double>& result);

/** @brief Get the area of the beam in the given unit for an image or a region applied to an image
 *  @param image The image or region applied to an image (casacore::SubImage)
 *  @param unit The unit to use for the area calculation, based on the image unit
 *  @param beam_area The result of the calculation
 *  @return Whether the calculation succeeded
 *  @details The beam area calculation fails when the image has no beam.
 *  casacore::ImageInfo is used to get the area of the beam.
 *  @see ComputeFluxDensity
 */
bool GetBeamArea(const casacore::ImageInterface<float>& image, const casacore::String unit, double& beam_area);

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_
