/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_IMAGEGENERATOR_H_
#define CARTA_BACKEND_IMAGEGENERATORS_IMAGEGENERATOR_H_

using GeneratorProgressCallback = std::function<void(float)>;

#define FIRST_PROGRESS_AFTER_MILLI_SECS 5000
#define PROGRESS_REPORT_INTERVAL 0.1
#define PROCESS_COMPLETED 1.0
#define ID_MULTIPLIER 1000

#include <casacore/images/Images/ImageInterface.h>

namespace carta {

struct CollapseResult {
    int file_id;
    std::string name;
    std::shared_ptr<casacore::ImageInterface<casacore::Float>> image;

    CollapseResult() {}
    CollapseResult(int file_id_, std::string name_, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image_) {
        file_id = file_id_;
        name = name_;
        image = image_;
    }
};

}

#endif // CARTA_BACKEND_IMAGEGENERATORS_IMAGEGENERATOR_H_
