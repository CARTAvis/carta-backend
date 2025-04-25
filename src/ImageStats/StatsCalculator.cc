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
#include <casacore/images/Images/ImageStatistics.h>

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

    casacore::Array<casacore::Double> num_points;
    image_stats.getStatistic(num_points, casacore::LatticeStatsBase::NPTS);

    for (size_t i = 0; i < requested_stats.size(); ++i) {
        // get requested statistics values
        auto carta_stats_type = requested_stats[i];
        casacore::LatticeStatsBase::StatisticsTypes lattice_stats_type(casacore::LatticeStatsBase::NSTATS);

        std::vector<double> dbl_result; // lattice stats
        std::vector<int> int_result;    // position stats

        if (!num_points.empty()) {
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
                        if (carta_stats_type == CARTA::StatsType::MinPos) {
                            int_result = (blc + min_pos).asStdVector();
                        } else { // MaxPos
                            int_result = (blc + max_pos).asStdVector();
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            casacore::Array<casacore::Double> result;
            if (carta_stats_type == CARTA::FluxDensity) {
                try {
                    // Flux density only valid for per-plane stats (not spectral profile), so one result per stat.
                    double npoints = num_points(casacore::IPosition(1, 0));
                    image_stats.getStatistic(result, casacore::LatticeStatsBase::SUM);
                    double sum = result(casacore::IPosition(1, 0));
                    double flux(0.0);
                    bool carta_flux_ok(false); // TODO: remove after testing

                    if (ComputeFluxDensity(image, npoints, sum, flux)) {
                        result.resize(casacore::IPosition(1, 1));
                        result[0] = flux;
                        result.tovector(dbl_result);
                        carta_flux_ok = true;
                    }

                    // Compare to casa ImageStatistics - TODO: remove after testing
                    result.resize();
                    image_stats.getStatistic(result, casacore::LatticeStatsBase::FLUX);
                    bool imstats_flux_ok = !result.empty();
                    if (carta_flux_ok && imstats_flux_ok) {
                        spdlog::debug("carta flux={} image stats flux={}", flux, result(casacore::IPosition(1, 0)));
                    } else if (carta_flux_ok) {
                        spdlog::debug("carta flux={} image stats flux failed", flux);
                    } else if (imstats_flux_ok) {
                        spdlog::debug("carta flux failed, image stats flux={}", result(casacore::IPosition(1, 0)));
                    }
                } catch (const casacore::AipsError& err) {
                    std::cerr << "Flux density exception: " << err.getMesg() << std::endl;
                    // Leave dbl_result empty, to be filled with nan.
                }
            } else if (lattice_stats_type < casacore::LatticeStatsBase::NSTATS) {
                try {
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
                    // Leave dbl_result empty, to be filled with nan.
                }
            }

            if (!int_result.empty()) {
                // Convert to double
                dbl_result.reserve(int_result.size());
                for (unsigned int j = 0; j < int_result.size(); ++j) {
                    dbl_result.push_back(static_cast<double>(int_result[j]));
                }
            }
        }

        if (dbl_result.empty()) {
            // Stat failed: set to NaN
            for (size_t j = 0; j < result_size; ++j) {
                dbl_result.push_back(nan(""));
            }
        }

        if (!dbl_result.empty()) {
            stats_values.emplace(carta_stats_type, dbl_result);
        }
    }

    return true;
}

bool ComputeFluxDensity(const casacore::ImageInterface<float>& image, double npixels, double sum, double& flux_density) {
    // Compute flux density when image has compatible units.  Value returned in flux_density.
    // Returns whether calculation succeeded.

    // Return if no image unit.
    casacore::String bunit(image.units().getName());
    if (bunit.empty()) {
        return false;
    }

    // Return if no pixels in image with finite data values.
    if (npixels == 0.0) {
        return false;
    }

    // Separate unit parts
    casacore::String flux_unit(bunit);
    casacore::String per_unit;
    if (bunit.contains("/")) {
        flux_unit = bunit.before("/");
        per_unit = bunit.after("/");
    }

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
        return true;
    } else {
        if (!flux_unit.contains("Jy")) {
            spdlog::warn("Cannot compute flux density for image unit not in Jy.");
            return false;
        }

        // Casacore supports "unit-1" and "/unit" syntax
        if (flux_unit.contains("pixel-1") || (per_unit == "pixel")) {
            flux_density = sum;
        } else if (flux_unit.contains("beam-1") || per_unit.contains("beam")) {
            double beam_area;
            if (!GetBeamArea(image, "sr", beam_area)) {
                // No beam defined
                return false;
            }
            flux_density = sum * beam_area;
        } else {
            // Get area for one pixel
            auto increments = image.coordinates().increment();
            auto units = image.coordinates().worldAxisUnits();
            casacore::Quantity area_unit = casacore::Quantity(1, units[0]) * casacore::Quantity(1, units[1]);
            casacore::Quantity pixel_area(fabs(increments[0] * increments[1]), area_unit.getUnit());

            // Convert pixel area to per_unit
            if (flux_unit.contains("sr-1") || (per_unit == "sr")) {
                double pixel_area_sr = pixel_area.get(per_unit).getValue();
                spdlog::debug("Using pixel area {} {} for flux density", pixel_area_sr, per_unit);
                flux_density = sum * pixel_area_sr;
            } else if (flux_unit.contains("arcsec-2") || (per_unit == "arcsec2")) {
                // Casacore supports various syntax for arcsec
                double pixel_area_per_unit = pixel_area.get("arcsec2").getValue();
                spdlog::debug("Using pixel area {} arcsec2 for flux density", pixel_area_per_unit);
                flux_density = sum * pixel_area_per_unit;
            } else {
                return false;
            }
        }
    }

    return true;
}

bool GetBeamArea(const casacore::ImageInterface<float>& image, const casacore::String unit, double& beam_area) {
    // Return restoring beam area in solid angle unit `unit` in `angle`.
    // Returns false if image has no restoring beam  or unit is not a solid angle unit.
    if (!image.imageInfo().hasBeam()) {
        spdlog::warn("Image has no beam for flux density");
        return false;
    }
    casacore::Quantity unit_q(1.0, unit);
    if (!unit_q.isConform("sr")) {
        spdlog::warn("Beam area unit {} is not solid angle", unit);
        return false;
    }

    beam_area = image.imageInfo().restoringBeam().getArea(unit);
    spdlog::debug("Using beam area {} {} for flux density", beam_area, unit);
    return true;
}

bool GetWavelength(const casacore::ImageInterface<float>& image, const casacore::String unit, double& wavelength) {
    // Use image spectral coordinate to convert frequency to wavelength in unit.
    // Returns false if image has no spectral axis or conversion fails.
    if (!image.coordinates().hasSpectralAxis()) {
        spdlog::warn("Image has no spectral axis, cannot compute flux density");
        return false;
    }

    auto spectral_coord = image.coordinates().spectralCoordinate();
    spectral_coord.setWavelengthUnit(unit);

    // Get frequency of current plane (spectral coordinate is 1D)
    double frequency;
    spectral_coord.toWorld(frequency, 0);
    casacore::String freq_unit = spectral_coord.worldAxisUnits()[0];

    // Convert freq to wave
    casacore::Vector<double> wavelen, freq(1, frequency);
    if (!spectral_coord.frequencyToWavelength(wavelen, freq)) {
        spdlog::warn("Cannot convert image frequency to wavelength for flux density");
        return false;
    }
    wavelength = wavelen[0];
    spdlog::debug("Converted current frequency {} {} to wavelength {} {} for flux density", frequency, freq_unit, wavelength, unit);
    return true;
}

} // namespace carta
