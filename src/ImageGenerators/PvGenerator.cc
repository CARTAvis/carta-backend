/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

#include <chrono>

#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>
#include <casacore/lattices/LRegions/LCRegionFixed.h>
#include <casacore/measures/Measures/Stokes.h>

#include "../ImageStats/StatsCalculator.h"
#include "../Logger/Logger.h"
#include "Util/FileSystem.h"
#include "Util/Image.h"

using namespace carta;

PvGenerator::PvGenerator(int file_id, const std::string& filename) : _stop_calc(false) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetPvFilename(filename);
}

void PvGenerator::CalculatePvImage(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
    double offset_increment, size_t num_channels, int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback,
    CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image) {
    // Calculate PV image with progress updates.  Returns PvResponse and GeneratedImage (generated file_id, pv filename, image).
    casacore::CoordinateSystem csys;
    loader->GetCoordinateSystem(csys);
    if (!csys.hasSpectralAxis()) {
        pv_response.set_success(false);
        pv_response.set_message("Cannot generate PV image with no spectral axis.");
        return;
    }

    // Find shape of first non-null region
    casacore::IPosition region_shape;
    for (auto region : box_regions) {
        if (region) {
            region_shape = region->shape();
            break;
        }
    }

    if (region_shape.empty()) {
        pv_response.set_success(false);
        pv_response.set_message("PV calculation failed for input region.");
        return;
    }

    if (loader->UseRegionSpectralData(region_shape, image_mutex)) {
        // Calculate using loader spectral profiles
        CalculatePvImageSpectral(
            loader, box_regions, offset_increment, num_channels, stokes, image_mutex, progress_callback, pv_response, pv_image);
    } else {
        // Calculate using stats spectral profiles
        std::unique_lock<std::mutex> ulock(image_mutex);
        CalculatePvImageStats(loader, box_regions, offset_increment, num_channels, stokes, progress_callback, pv_response, pv_image);
        ulock.unlock();
    }
}

void PvGenerator::CalculatePvImageStats(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
    double offset_increment, size_t num_channels, int stokes, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
    carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through regions for spectral profiles from stats calculator
    size_t num_regions(box_regions.size());
    float progress(0.0);

    // PV values in new image
    casacore::Matrix<float> pv_values(num_regions, num_channels);
    pv_values = NAN;

    // Assume we will finish
    pv_response.set_cancel(false);

    for (size_t iregion = 0; iregion < num_regions; ++iregion) {
        // Check for cancel
        if (_stop_calc) {
            pv_response.set_message("PV generator cancelled.");
            pv_response.set_cancel(true);
            break;
        }

        if (!box_regions[iregion]) {
            // Progress update
            progress = float(iregion + 1) / float(num_regions);
            if (progress < 1.0) {
                progress_callback(progress);
            }

            continue;
        }

        // Apply extension/intersection for all channels and stokes
        auto image_region = GetImageRegion(loader, box_regions[iregion], stokes);

        // Create casacore SubImage from ImageRegion for region stats
        casacore::SubImage<float> sub_image;
        if (!loader->GetSubImage(image_region, sub_image)) {
            pv_response.set_message("PV generator subimage from region failed.");
            break;
        }

        // Get region stats mean
        std::map<CARTA::StatsType, std::vector<double>> results;
        std::vector<CARTA::StatsType> required_stats = {CARTA::StatsType::Mean};
        bool per_z(true);
        CalcStatsValues(results, required_stats, sub_image, per_z);

        // Set region row
        auto spectral_profile = results[CARTA::StatsType::Mean];
        pv_values.row(iregion) = spectral_profile;

        // Progress update
        progress = float(iregion + 1) / float(num_regions);
        if (progress < 1.0) {
            progress_callback(progress);
        }
    }

    if (progress == 1.0) {
        // Determine whether stokes axis
        casacore::IPosition pv_shape = GetPvImageShape(loader, num_regions, num_channels);
        // Create casacore::TempImage
        SetupPvImage(loader->GetImage(), pv_shape, stokes, offset_increment);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // Set returned image
        pv_image = GetGeneratedImage();

        pv_response.set_success(true);
    } else {
        pv_response.set_success(false);
        pv_response.set_message("PV generator failed to complete.");
    }
}

void PvGenerator::CalculatePvImageSpectral(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
    double offset_increment, size_t num_channels, int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback,
    CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through regions for spectral profiles from loader
    size_t num_regions(box_regions.size());
    float progress(0.0);

    // PV values in new image
    casacore::Matrix<float> pv_values(num_regions, num_channels);
    pv_values = NAN;

    // Assume we will finish
    pv_response.set_cancel(false);

    casacore::IPosition image_shape;
    loader->GetShape(image_shape);

    for (size_t iregion = 0; iregion < num_regions; ++iregion) {
        // Check for cancel
        if (_stop_calc) {
            pv_response.set_message("PV generator cancelled.");
            pv_response.set_cancel(true);
            break;
        }

        if (!box_regions[iregion]) {
            continue;
        }

        // Get region mask and origin
        casacore::ArrayLattice<casacore::Bool> mask;
        casacore::IPosition origin;
        // Region is either extended or fixed
        auto extended_region = dynamic_cast<casacore::LCExtension*>(box_regions[iregion]);
        if (extended_region) {
            auto& fixed_region = static_cast<const casacore::LCRegionFixed&>(extended_region->region());
            mask = fixed_region.getMask();
            origin = fixed_region.boundingBox().start();
        } else {
            auto fixed_region = dynamic_cast<casacore::LCRegionFixed*>(box_regions[iregion]);
            mask = fixed_region->getMask();
            origin = fixed_region->boundingBox().start();
        }

        // Get region spectral profile (mean) from loader
        std::map<CARTA::StatsType, std::vector<double>> results;
        float spectral_progress(0.0);

        while (spectral_progress < 1.0) {
            if (!loader->GetRegionSpectralData(TEMP_REGION_ID, stokes, mask, origin, image_mutex, results, spectral_progress)) {
                pv_response.set_message("PV generator spectral profile failed.");
                break;
            }
        }

        // Set region row
        auto spectral_profile = results[CARTA::StatsType::Mean];
        pv_values.row(iregion) = spectral_profile;

        // Progress update
        progress = (iregion + 1) / num_regions;
        if (progress < 1.0) {
            progress_callback(progress);
        }
    }

    if (progress == 1.0) {
        // Determine whether stokes axis
        casacore::IPosition pv_shape = GetPvImageShape(loader, num_regions, num_channels);
        // Create casacore::TempImage
        SetupPvImage(loader->GetImage(), pv_shape, stokes, offset_increment);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // Set returned image
        pv_image = GetGeneratedImage();

        pv_response.set_success(true);
    } else {
        pv_response.set_success(false);
    }
}

void PvGenerator::StopCalculation() {
    _stop_calc = true;
}

std::string PvGenerator::GetPvFilename(const std::string& filename) {
    // image.ext -> image_pv.ext
    fs::path input_filepath(filename);

    // Assemble new filename
    auto pv_path = input_filepath.stem();
    pv_path += "_pv";
    pv_path += input_filepath.extension();

    return pv_path.string();
}

casacore::ImageRegion PvGenerator::GetImageRegion(std::shared_ptr<carta::FileLoader> loader, casacore::LCRegion* lcregion, int stokes) {
    // Create LCExtension or LCIntersection for stokes
    casacore::IPosition image_shape;
    int spectral_axis, z_axis, stokes_axis;
    std::string message;
    loader->FindCoordinateAxes(image_shape, spectral_axis, z_axis, stokes_axis, message);

    auto region_shape = lcregion->shape();
    auto ndim = image_shape.size();

    // Make slicer with single channel and stokes.
    casacore::IPosition start(ndim, 0), end(image_shape);
    if (stokes_axis >= 0) {
        start(stokes_axis) = stokes;
        end(stokes_axis) = stokes;
    }
    casacore::Slicer slicer(start, end, casacore::Slicer::endIsLast);

    // Create ImageRegion from LCRegion intersection or extension
    casacore::ImageRegion image_region;

    if (region_shape.size() == ndim) {
        // Region includes all channels and stokes; limit with intersection box from slicer.
        casacore::LCBox box(slicer, image_shape);
        casacore::LCIntersection intersection_region(*lcregion, box);
        image_region = casacore::ImageRegion(intersection_region);
    } else {
        // Region only in xy plane; extend with channel/stokes extension box.
        // Create box with chan/stokes slicer and extend region in chan/stokes axes.
        casacore::IPosition xy_axes(2, 0, 1), image_axes(casacore::IPosition::makeAxisPath(ndim));
        casacore::Slicer chan_stokes_slicer =
            casacore::Slicer(slicer.start().removeAxes(xy_axes), slicer.end().removeAxes(xy_axes), casacore::Slicer::endIsLast);
        casacore::IPosition extend_axes = image_axes.removeAxes(xy_axes);

        casacore::LCBox extend_box(chan_stokes_slicer, image_shape.removeAxes(xy_axes));
        casacore::LCExtension extended_region(*lcregion, extend_axes, extend_box);
        image_region = casacore::ImageRegion(extended_region);
    }

    return image_region;
}

casacore::IPosition PvGenerator::GetPvImageShape(std::shared_ptr<carta::FileLoader> loader, size_t num_regions, size_t num_channels) {
    // Return expected shape of pv image data (stokes axis?)
    casacore::IPosition pv_shape;

    casacore::CoordinateSystem csys;
    if (loader->GetCoordinateSystem(csys) && csys.hasPolarizationCoordinate()) {
        pv_shape = casacore::IPosition(3, num_regions, num_channels, 1);
    } else {
        pv_shape = casacore::IPosition(2, num_regions, num_channels);
    }

    return pv_shape;
}

void PvGenerator::SetupPvImage(
    std::shared_ptr<casacore::ImageInterface<float>> input_image, casacore::IPosition& pv_shape, int stokes, double offset_increment) {
    // Create coordinate system and temp image
    const casacore::CoordinateSystem input_csys(input_image->coordinates());
    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(input_csys, pv_shape, stokes, offset_increment);

    _image.reset(new casacore::TempImage<casacore::Float>(casacore::TiledShape(pv_shape), pv_csys));

    _image->setUnits(input_image->units());
    _image->setMiscInfo(input_image->miscInfo());
    _image->setImageInfo(input_image->imageInfo());
    _image->appendLog(input_image->logger());
}

casacore::CoordinateSystem PvGenerator::GetPvCoordinateSystem(
    const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape, int stokes, double offset_increment) {
    // Set PV coordinate system with LinearCoordinate and input coordinates for spectral and stokes
    casacore::CoordinateSystem csys;

    // Add linear coordinate (offset); needs to have 2 axes or pc matrix will fail in wcslib.
    // Will remove degenerate linear axis below
    casacore::Vector<casacore::String> name(2, "Offset");
    casacore::Vector<casacore::String> unit(2, "arcsec");
    casacore::Vector<casacore::Double> crval(2, 0.0); // center offset is 0
    casacore::Vector<casacore::Double> inc(2, offset_increment);
    casacore::Matrix<casacore::Double> pc(2, 2, 1);
    pc(0, 1) = 0.0;
    pc(1, 0) = 0.0;
    casacore::Vector<casacore::Double> crpix(2, (pv_shape[0] - 1) / 2);
    casacore::LinearCoordinate linear_coord(name, unit, crval, inc, pc, crpix);
    csys.addCoordinate(linear_coord);

    // Add spectral coordinate
    csys.addCoordinate(input_csys.spectralCoordinate());

    // Add stokes coordinate if input image has one
    if (input_csys.hasPolarizationCoordinate()) {
        auto stokes_type = casacore::Stokes::type(stokes + 1);
        casacore::Vector<casacore::Int> types(1, stokes_type);
        casacore::StokesCoordinate stokes_coord(types);
        csys.addCoordinate(stokes_coord);
    }

    // Remove second linear axis
    csys.removeWorldAxis(1, 0.0);

    return csys;
}

GeneratedImage PvGenerator::GetGeneratedImage() {
    // Set GeneratedImage struct
    GeneratedImage pv_image(_file_id, _name, _image);
    return pv_image;
}
