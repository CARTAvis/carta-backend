/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_
#define CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_

#include <casacore/images/Images/ImageInterface.h>

#include <functional>

using GeneratorProgressCallback = std::function<void(float)>;

namespace carta {

struct GeneratedImage {
    std::string name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image;

    GeneratedImage() {}
    GeneratedImage(std::string name_, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image_) {
        name = name_;
        image = image_;
    }
};

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_IMAGEGENERATOR_H_
