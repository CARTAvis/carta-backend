/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_
#define CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_

#include <casacore/images/Images/ImageInterface.h>

#include <functional>

#define MOMENT_ID_MULTIPLIER 1000
#define PV_ID_MULTIPLIER -1000
#define FITTING_ID_MULTIPLIER -1000
#define FITTING_WITH_MOMENT_ID_MULTIPLIER -10

using GeneratorProgressCallback = std::function<void(float)>;

namespace carta {

struct GeneratedImage {
    int file_id;
    std::string name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image;

    GeneratedImage() {}
    GeneratedImage(int file_id_, std::string name_, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image_) {
        file_id = file_id_;
        name = name_;
        image = image_;
    }
};

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_
