/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_PVGENERATOR_H_
#define CARTA_SRC_IMAGEGENERATORS_PVGENERATOR_H_

#include <casacore/images/Images/TempImage.h>
#include <casacore/lattices/LRegions/LCRegion.h>

#include <carta-protobuf/pv_request.pb.h>

#include "Frame/Frame.h"
#include "ImageData/FileLoader.h"
#include "ImageGenerator.h"

namespace carta {

class PvGenerator {
public:
    enum PositionAxisType {OFFSET, DISTANCE};

    PvGenerator();
    ~PvGenerator() = default;

    // For PV generator (not preview)
    void SetFileName(int index, const std::string& filename, bool is_preview = false);

    // Create generated PV image from input data.
    // cut_region_type determines position axis as offset from center (line) or distance from beginning (polyline).
    // reverse determines order of position and spectral axes.
    // Returns generated image and success, with message if failure.
    bool GetPvImage(const std::shared_ptr<Frame>& frame, const casacore::Matrix<float>& pv_data, casacore::IPosition& pv_shape,
        PositionAxisType axis_type, const casacore::Quantity& position_increment, int start_chan, int stokes, bool reverse,
        GeneratedImage& pv_image, std::string& message);

private:
    void SetPvImageName(const std::string& filename, int index, bool is_preview);

    std::shared_ptr<casacore::ImageInterface<casacore::Float>> SetupPvImage(
        const std::shared_ptr<casacore::ImageInterface<float>>& input_image, const std::shared_ptr<casacore::CoordinateSystem>& input_csys,
        casacore::IPosition& pv_shape, const std::string& position_name, const casacore::Quantity& position_increment,
        double position_refval, double position_refpix, double spectral_refval, int stokes, bool reverse, std::string& message);
    casacore::CoordinateSystem GetPvCoordinateSystem(const std::shared_ptr<casacore::CoordinateSystem>& input_csys,
        casacore::IPosition& pv_shape, const std::string& position_name, const casacore::Quantity& position_increment,
        double position_refval, double position_refpix, double spectral_refval, int stokes, bool reverse);

    // GeneratedImage parameters
    int _file_id;
    std::string _name;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_PVGENERATOR_H_
