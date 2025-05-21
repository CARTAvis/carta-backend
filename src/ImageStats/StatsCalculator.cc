/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# StatsCalculator.cc: functions for calculating statistics and histograms

#include "StatsCalculator.h"

#include <cmath>
#include <limits>

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/BasicSL/Constants.h>
#include <casacore/casa/Quanta/QC.h>

#include "Logger/Logger.h"

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
    casacore::ImageStatistics<float> image_stats(image, /*showProgress*/ false, /*forceDisk*/ false, /*clone*/ false);

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

    // num_points used for setting stats results to NaN where num_points is zero.
    casacore::Array<casacore::Double> num_points;
    image_stats.getStatistic(num_points, casacore::LatticeStatsBase::NPTS);

    if (!num_points.empty()) {
        std::vector<double> dbl_result;           // return value in stats_value map
        casacore::Array<casacore::Double> result; // ImageStatistics result

        for (size_t i = 0; i < requested_stats.size(); ++i) {
            // get requested statistics values
            auto carta_stats_type = requested_stats[i];

            // Clear previous results
            dbl_result.clear();

            switch (carta_stats_type) {
                case CARTA::StatsType::NumPixels: {
                    num_points.tovector(dbl_result); // Already retrieved before loop
                    break;
                }
                case CARTA::StatsType::Blc:
                case CARTA::StatsType::Trc:
                case CARTA::StatsType::MinPos:
                case CARTA::StatsType::MaxPos: {
                    GetPositionStats(image, image_stats, carta_stats_type, dbl_result);
                    break;
                }
                case CARTA::StatsType::FluxDensity: {
                    ComputeFluxDensity(image, image_stats, dbl_result);
                    break;
                }
                default: {
                    try {
                        casacore::LatticeStatsBase::StatisticsTypes lattice_stats_type = carta_stats_to_casacore.at(carta_stats_type);
                        result.resize();

                        if (image_stats.getStatistic(result, lattice_stats_type)) {
                            // return result Array for stats type
                            if (anyEQ(result, 0.0)) {
                                // Convert any 0 result to NaN if number of points is zero
                                for (size_t j = 0; j < result.size(); ++j) {
                                    casacore::IPosition index(1, j);
                                    if ((result(index) == 0.0) && (num_points(index) == 0.0)) {
                                        result(index) = nan("");
                                    }
                                }
                            }

                            if (carta_stats_type == CARTA::StatsType::Extrema) {
                                // Result is MIN
                                std::vector<double> min_result;
                                result.tovector(min_result);
                                // Get MAX
                                if (image_stats.getStatistic(result, casacore::LatticeStatsBase::MAX)) {
                                    std::vector<double> max_result;
                                    result.tovector(max_result);
                                    // Result is greater of abs(MIN) and abs(MAX), inserted in dbl_result
                                    std::transform(min_result.begin(), min_result.end(), max_result.begin(), std::back_inserter(dbl_result),
                                        [](double min, double max) { return (abs(min) > abs(max) ? min : max); });
                                }
                            } else {
                                result.tovector(dbl_result);
                            }
                        }
                    } catch (const casacore::AipsError& err) {
                        // Leave dbl_result empty, to be filled with nan.
                    } catch (const std::out_of_range& err) {
                        // Should not happen, all remaining stats types covered in map.
                    }
                    break;
                }
            }

            if (dbl_result.empty()) {
                // Stat failed: set to NaN
                for (size_t j = 0; j < result_size; ++j) {
                    dbl_result.push_back(nan(""));
                }
            }
            stats_values.emplace(carta_stats_type, dbl_result);
        }
    }

    return true;
}

void GetPositionStats(const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats,
    CARTA::StatsType carta_stats_type, std::vector<double>& result) {
    // Calculate position-related stats and return in dbl_result
    std::vector<int> int_result;

    switch (carta_stats_type) {
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
            const casacore::IPosition blc(image.region().slicer().start());
            casacore::IPosition min_pos, max_pos;
            image_stats.getMinMaxPos(min_pos, max_pos);

            if (carta_stats_type == CARTA::StatsType::MinPos) {
                int_result = (blc + min_pos).asStdVector();
            } else { // MaxPos
                int_result = (blc + max_pos).asStdVector();
            }
            break;
        }
        default:
            break;
    }

    if (!int_result.empty()) {
        // Convert to double
        result.reserve(int_result.size());
        for (unsigned int j = 0; j < int_result.size(); ++j) {
            result.push_back(static_cast<double>(int_result[j]));
        }
    }
}

bool ComputeFluxDensity(
    const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats, std::vector<double>& result) {
    // Compute flux density when image has compatible units.  Value returned in flux_density.
    // Returns whether calculation succeeded.
    // Return if no image unit.
    casacore::String bunit(image.units().getName());
    if (bunit.empty()) {
        return false;
    }

    casacore::Array<casacore::Double> stats_result;
    if (!image_stats.getStatistic(stats_result, casacore::LatticeStatsBase::NPTS)) {
        return false;
    }
    double npixels = stats_result(casacore::IPosition(1, 0));
    if (npixels == 0.0) {
        return false;
    }

    stats_result.resize();
    if (!image_stats.getStatistic(stats_result, casacore::LatticeStatsBase::SUM)) {
        return false;
    }
    double sum = stats_result(casacore::IPosition(1, 0));

    // Separate unit parts
    casacore::String flux_unit(bunit);
    casacore::String per_unit;
    if (bunit.contains("/")) {
        flux_unit = bunit.before("/");
        per_unit = bunit.after("/");
    }

    double flux_density;
    try {
        if (flux_unit.startsWith("K")) {
            // Convert sum to Jy using wavelength from spectral axis, beam solid angle, and constants.
            // Get beam solid angle in steradians
            double beam_area_sr;
            if (!GetBeamArea(image, "sr", beam_area_sr)) {
                return false; // no beam
            }

            // Get wavelength in meters
            double wavelength_m;
            if (!GetWavelength(image, "m", wavelength_m)) {
                return false;
            }

            // k() is Boltzmann's constant Quantity defined in QC.h
            flux_density = 2 * casacore::QC::k().getValue() * sum / std::pow(wavelength_m, 2) * beam_area_sr;
        } else {
            if (!flux_unit.contains("Jy")) {
                spdlog::warn("Cannot compute flux density for image unit not in Jy.");
                return false;
            }

            // Casacore supports "unit-1" and "/unit" syntax so check for both
            if (per_unit.empty() || flux_unit.contains("pixel-1") || (per_unit == "pixel")) {
                flux_density = sum;
            } else {
                // Get area for one pixel
                auto increments = image.coordinates().increment();
                auto units = image.coordinates().worldAxisUnits();
                casacore::Quantity area_unit = casacore::Quantity(1.0, units[0]) * casacore::Quantity(1.0, units[1]);
                casacore::Quantity pixel_area(std::fabs(increments[0] * increments[1]), area_unit.getUnit());

                // Convert pixel area to per_unit
                if (flux_unit.contains("beam-1") || per_unit == "beam") {
                    double pixel_area_sr = pixel_area.get("sr").getValue();
                    double beam_area_sr;
                    if (!GetBeamArea(image, "sr", beam_area_sr)) {
                        return false;
                    }
                    flux_density = sum * pixel_area_sr / beam_area_sr;
                } else if (flux_unit.contains("sr-1") || per_unit == "sr") {
                    double pixel_area_sr = pixel_area.get("sr").getValue();
                    flux_density = sum * pixel_area_sr;
                } else if (flux_unit.contains("arcsec-2") || per_unit == "arcsec2") {
                    double pixel_area_arcsec2 = pixel_area.get("arcsec2").getValue();
                    flux_density = sum * pixel_area_arcsec2;
                }
            }
        }
    } catch (const casacore::AipsError& err) {
        return false;
    }

    result.push_back(flux_density);
    return true;
}

bool GetBeamArea(const casacore::ImageInterface<float>& image, const casacore::String unit, double& beam_area) {
    // Return restoring beam area in solid angle unit `unit` in `angle`.
    // Returns false if image has no restoring beam  or unit is not a solid angle unit.
    if (!image.imageInfo().hasBeam()) {
        spdlog::warn("Image has no beam for flux density");
        return false;
    }

    beam_area = image.imageInfo().restoringBeam().getArea(unit);
    return true;
}

bool GetWavelength(const casacore::ImageInterface<float>& image, const casacore::String unit, double& wavelength) {
    // Use image spectral coordinate to convert frequency to wavelength in unit.
    // Returns false if image has no spectral axis or conversion fails.
    if (!image.coordinates().hasSpectralAxis()) {
        spdlog::warn("Image has no spectral axis for wavelength, cannot compute flux density");
        return false;
    }

    auto spectral_coord = image.coordinates().spectralCoordinate();
    spectral_coord.setWavelengthUnit(unit);

    // Get current frequency = spectral pixel 0 (spectral coordinate is 1D)
    double frequency;
    spectral_coord.toWorld(frequency, 0);

    // Convert freq to wave
    casacore::Vector<double> wavelen, freq(1, frequency);
    if (!spectral_coord.frequencyToWavelength(wavelen, freq)) {
        spdlog::warn("Cannot convert image frequency to wavelength for flux density");
        return false;
    }
    wavelength = wavelen[0];
    return true;
}

} // namespace carta
