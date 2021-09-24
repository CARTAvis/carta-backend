/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

using namespace carta;

PvGenerator::PvGenerator(int file_id, const std::string& filename) : _stop_calc(false) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetOutputFilename(filename);
}

void PvGenerator::CalculatePvImage(std::shared_ptr<carta::FileLoader>& loader, const std::vector<casacore::LCRegion*>& box_regions,
    size_t num_channels, int stokes, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
    carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through channels for region stats
    size_t num_regions(box_regions.size());
    float progress(0.0);

    casacore::IPosition image_shape(2, num_regions, num_channels); // TODO: stokes!
    casacore::Matrix<float> pv_values(image_shape);

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

            // TODO: Create ImageRegion
            // TODO: Get region stats mean for ImageRegion
			// region_mean_values(iregion) = mean;
        }

        pv_values.row(ichan) = region_mean_values;

        // Progress update
        progress = (ichan + 1) / num_channels;
        if (progress < CALCULATION_COMPLETE) {
            progress_callback(progress);
        }
    }

    if (progress == CALCULATION_COMPLETE) {
        SetupPvImage(loader->GetImage(), image_shape);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // TODO: Set returned image
        // pv_image = GetGeneratedImage();
    }
}

void PvGenerator::CalculatePvImage(std::shared_ptr<carta::FileLoader>& loader, const std::vector<casacore::LCRegion*>& box_regions,
    int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
    carta::GeneratedImage& pv_image) {
    // Generate PV image from line box regions: iterate through regions for spectral profiles
    size_t num_regions(box_regions.size()), num_channels(0);
    float progress(0.0);

    casacore::Matrix<float> pv_values;
    casacore::IPosition image_shape;

    for (size_t iregion = 0; iregion < num_regions; ++iregion) {
        // Check for cancel
        if (_stop_calc) {
            pv_response.set_message("PV generator cancelled.");
            pv_response.set_cancel(true);
            break;
        }

        // TODO: Get region mask and origin
        // TODO: Get region spectral data (mean) from loader
        // TODO: Set num_channels

        if (pv_values.empty()) {
            image_shape = casacore::IPosition(2, num_regions, num_channels); // TODO: stokes!
            pv_values.resize(image_shape);
        }

        // TODO: Set spectral profile data in pv_values
        // pv_values.column(iregion) = region_spectral_profile;

        // Progress update
        progress = (iregion + 1) / num_regions;
        if (progress < CALCULATION_COMPLETE) {
            progress_callback(progress);
        }
    }


    if (progress == CALCULATION_COMPLETE) {
        SetupPvImage(loader->GetImage(), image_shape);

        // Set data in temp image
        _image->put(pv_values);
        _image->flush();

        // TODO: Set returned image
        // pv_image = GetGeneratedImage();
    }
}

std::string PvGenerator::GetOutputFilename(const std::string& filename) {
    // TODO: image.ext -> image_pv.ext
    std::string result(filename);
    result += ".pv"; // image.ext.pv
    return result;
}

void PvGenerator::SetupPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, casacore::IPosition& image_shape) {
    // Create coordinate system and temp image
    casacore::CoordinateSystem pv_csys = GetPvCoordinateSystem(input_image);
    _image.reset(new casacore::TempImage<casacore::Float>(casacore::TiledShape(image_shape), pv_csys));

    _image->setUnits(input_image->units());
    _image->setMiscInfo(input_image->miscInfo());
    _image->setImageInfo(input_image->imageInfo());
    _image->appendLog(input_image->logger());
    _image->makeMask("mask0", true, true);
}

casacore::CoordinateSystem PvGenerator::GetPvCoordinateSystem(std::shared_ptr<casacore::ImageInterface<float>> input_image) {
    // TODO: set new_csys from image->coordinates()
    casacore::CoordinateSystem csys;
    return csys;
}

GeneratedImage PvGenerator::GetGeneratedImage() {
    // Set GeneratedImage struct
    GeneratedImage pv_image(_file_id, _name, _image);
    return pv_image;
}
