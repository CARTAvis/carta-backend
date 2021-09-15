/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvGenerator.h"

using namespace carta;

PvGenerator::PvGenerator(int file_id, const std::string& filename, const casacore::ImageInterface<float>* image) {
    _file_id = (file_id + 1) * ID_MULTIPLIER;
    _name = GetOutputFilename(filename);
    InitializePvImage(image);
}

std::string PvGenerator::GetOutputFilename(const std::string& filename) {
    // TODO: image.ext -> image_pv.ext
    std::string result(filename);
    result += ".pv"; // image.ext.pv
    return result;
}

void PvGenerator::InitializePvImage(const casacore::ImageInterface<float>* image) {
    /*
    // TODO: Use input image parts for output pv image
    _image_csys = image->coordinateSystem();
    _image_info = image->imageInfo();
    _image_shape = image->shape();
    */
}

GeneratedImage PvGenerator::GetImage() {
    std::shared_ptr<casacore::TempImage<casacore::Float>> temp_image(_image);
    GeneratedImage pv_image(_file_id, _name, temp_image);
    return pv_image;
}
