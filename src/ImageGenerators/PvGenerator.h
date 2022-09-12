/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    PvGenerator(int file_id, const std::string& filename, int index);

    // Normally set up as x=offset, y=spectral.  If reverse=true, set up x=spectral, y=offset.
    // Returns generated pv_image or message if failure.
    bool GetPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, const casacore::Matrix<float>& pv_data,
        const casacore::Quantity& offset_increment, int stokes, bool reverse, GeneratedImage& pv_image, std::string& message);

private:
    std::string GetPvFilename(const std::string& filename, int index);

    bool SetupPvImage(std::shared_ptr<casacore::ImageInterface<float>> input_image, casacore::IPosition& pv_shape, int stokes,
        const casacore::Quantity& offset_increment, bool reverse, std::string& message);
    casacore::CoordinateSystem GetPvCoordinateSystem(const casacore::CoordinateSystem& input_csys, casacore::IPosition& pv_shape,
        int stokes, const casacore::Quantity& offset_increment, bool reverse);
    GeneratedImage GetGeneratedImage();

    // GeneratedImage parameters
    int _file_id;
    std::string _name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> _image;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_
