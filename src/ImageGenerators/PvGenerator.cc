/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <casacore/coordinates/Coordinates/LinearCoordinate.h>
#include <casacore/coordinates/Coordinates/SpectralCoordinate.h>
#include <casacore/coordinates/Coordinates/StokesCoordinate.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/measures/Measures/Stokes.h>

#include "../ImageStats/StatsCalculator.h"
#include "PvGenerator.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace carta;

PvGenerator::PvGenerator(int file_id, const std::string& filename) : _stop_calc(false) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetPvFilename(filename);
}

void PvGenerator::CalculatePvImage(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::ImageRegion>& box_regions,
    double offset_increment, size_t num_channels, int stokes, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
    carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through channels for region stats
    size_t num_regions(box_regions.size());
    float progress(0.0);

    casacore::Matrix<float> pv_values(num_regions, num_channels);

    // Assume we will finish
    pv_response.set_cancel(false);

    for (size_t ichan = 0; ichan < num_channels; ++ichan) {
        // One mean value per region
        casacore::Vector<float> region_mean_values(num_regions, 0.0);

        for (size_t iregion = 0; iregion < num_regions; ++iregion) {
            // Check for cancel
            if (_stop_calc) {
                pv_response.set_message("PV generator cancelled.");
                pv_response.set_cancel(true);
                break;
            }

            // Create casacore SubImage from ImageRegion for region stats
            casacore::SubImage<float> sub_image;
            if (!loader->GetSubImage(box_regions[iregion], sub_image)) {
                pv_response.set_message("PV generator subimage from region failed.");
                break;
            }

            // Get region stats mean
            std::map<CARTA::StatsType, std::vector<double>> results;
            std::vector<CARTA::StatsType> required_stats = {CARTA::StatsType::Mean};
            CalcStatsValues(results, required_stats, sub_image);

            // Set PV value
            region_mean_values(iregion) = results[CARTA::StatsType::Mean][0]; // one value for one channel
        }

        pv_values.column(ichan) = region_mean_values;

        // Progress update
        progress = (ichan + 1) / num_channels;
        if (progress < 1.0) {
            progress_callback(progress);
        }
    }

    if (progress == 1.0) {
        casacore::IPosition pv_shape = GetPvImageShape(loader, num_regions, num_channels);
        SetupPvImage(loader->GetImage(), pv_shape, stokes, offset_increment);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // Set returned image
        pv_image = GetGeneratedImage();
    }
}

void PvGenerator::CalculatePvImage(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::ImageRegion>& box_regions,
    double offset_increment, int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback,
    CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through regions for spectral profiles
    size_t num_regions(box_regions.size()), num_channels(0);
    float progress(0.0);

    casacore::Matrix<float> pv_values;

    for (size_t iregion = 0; iregion < num_regions; ++iregion) {
        // Check for cancel
        if (_stop_calc) {
            pv_response.set_message("PV generator cancelled.");
            pv_response.set_cancel(true);
            break;
        }

        auto fixed_region = dynamic_cast<const casacore::LCRegionFixed*>(&box_regions[iregion].asLCRegion());
        auto mask = fixed_region->getMask();
        auto origin = fixed_region->boundingBox().start();

        // Get region spectral profile (mean) from loader
        std::map<CARTA::StatsType, std::vector<double>> results;
        float spectral_progress(0.0);

        while (spectral_progress < 1.0) {
            if (!loader->GetRegionSpectralData(TEMP_REGION_ID, stokes, mask, origin, image_mutex, results, spectral_progress)) {
                pv_response.set_message("PV generator spectral profile failed.");
                break;
            }
        }

        auto spectral_profile = results[CARTA::StatsType::Mean];

        if (pv_values.empty()) {
            num_channels = spectral_profile.size();
            pv_values.resize(casacore::IPosition(2, num_regions, num_channels));
        }

        pv_values.row(iregion) = spectral_profile;

        // Progress update
        progress = (iregion + 1) / num_regions;
        if (progress < 1.0) {
            progress_callback(progress);
        }
    }

    if (progress == 1.0) {
        casacore::IPosition pv_shape = GetPvImageShape(loader, num_regions, num_channels);
        SetupPvImage(loader->GetImage(), pv_shape, stokes, offset_increment);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // Set returned image
        pv_image = GetGeneratedImage();
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
    _image->makeMask("mask0", true, true);
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
        auto stokes_type = casacore::Stokes::type(stokes);
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
