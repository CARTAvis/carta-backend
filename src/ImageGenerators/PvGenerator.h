/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_

#include <casacore/images/Images/TempImage.h>

#include "ImageGenerator.h"

namespace carta {

class PvGenerator {
public:
    PvGenerator(int file_id, const std::string& filename, const casacore::ImageInterface<float>* image);
    ~PvGenerator(){};

    GeneratedImage GetImage();

private:
    std::string GetOutputFilename(const std::string& filename);
    void InitializePvImage(const casacore::ImageInterface<float>* image);

    // GeneratedImage parameters
    int _file_id;
    std::string _name;
    casacore::TempImage<casacore::Float>* _image;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVGENERATOR_H_
