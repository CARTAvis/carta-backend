/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__IMAGEDATA_STOKESFILESCONNECTOR_H_
#define CARTA_BACKEND__IMAGEDATA_STOKESFILESCONNECTOR_H_

#include <carta-protobuf/concat_stokes_files.pb.h>
#include <casacore/images/Images/ExtendImage.h>
#include <casacore/images/Images/ImageConcat.h>

#include "FileLoader.h"

namespace carta {

class StokesFilesConnector {
public:
    StokesFilesConnector(const std::string& root_folder);
    ~StokesFilesConnector();

    bool DoConcat(const CARTA::ConcatStokesFiles& message, CARTA::ConcatStokesFilesAck& response,
        std::shared_ptr<casacore::ImageConcat<float>>& concatenated_image, std::string& concatenated_name);
    void ClearCache();

private:
    bool OpenStokesFiles(const CARTA::ConcatStokesFiles& message, std::string& err);
    bool StokesFilesValid(std::string& err, int& stokes_axis);
    static bool GetCasaStokesType(const CARTA::PolarizationType& stokes_type, casacore::Stokes::StokesTypes& result);

    std::string _top_level_folder;
    std::string _concatenated_name;
    std::unordered_map<CARTA::PolarizationType, std::unique_ptr<FileLoader<float>>> _loaders;
};

} // namespace carta

#endif // CARTA_BACKEND__IMAGEDATA_STOKESFILESCONNECTOR_H_
