/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_

#include <casacore/images/Images/TempImage.h>
#include <casacore/lattices/LRegions/LCRegion.h>

#include <carta-protobuf/pv_request.pb.h>

#include "../ImageData/FileLoader.h"
#include "ImageGenerator.h"

namespace carta {

class PvGenerator {
public:
    PvGenerator(int file_id, const std::string& filename);
    ~PvGenerator(){};

    void CalculatePvImage(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
        double offset_increment, size_t num_channels, int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image);
    void StopCalculation();

private:
    std::string GetPvFilename(const std::string& filename);

    // Calculate stats for each region in each channel
    void CalculatePvImageStats(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
        double offset_increment, size_t num_channels, int stokes, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image);
    // Calculate spectral profile for each region
    void CalculatePvImageSpectral(std::shared_ptr<carta::FileLoader> loader, const std::vector<casacore::LCRegion*>& box_regions,
        double offset_increment, size_t num_channels, int stokes, std::mutex& image_mutex, GeneratorProgressCallback progress_callback,
        CARTA::PvResponse& pv_response, carta::GeneratedImage& pv_image);

    casacore::ImageRegion GetImageRegion(std::shared_ptr<carta::FileLoader> loader, casacore::LCRegion* lcregion, int chan, int stokes);
    casacore::IPosition GetPvImageShape(std::shared_ptr<carta::FileLoader> loader, size_t num_regions, size_t num_channels);
    void SetupPvImage(
        std::shared_ptr<casacore::ImageInterface<float>> input_image, casacore::IPosition& pv_shape, int stokes, double offset_increment);
    casacore::CoordinateSystem GetPvCoordinateSystem(
        const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape, int stokes, double offset_increment);
    casacore::CoordinateSystem GetPvCoordinateSystem(const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape);
    GeneratedImage GetGeneratedImage();

    // GeneratedImage parameters
    int _file_id;
    std::string _name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> _image;

    bool _stop_calc;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_
