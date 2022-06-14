/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileInfoLoader.h: load FileInfo fields for given file

#ifndef CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_

#include <string>

#include <carta-protobuf/file_info.pb.h>

namespace carta {

class FileInfoLoader {
public:
    FileInfoLoader(const std::string& filename);
    FileInfoLoader(const std::string& filename, const CARTA::FileType& type);

    bool FillFileInfo(CARTA::FileInfo& file_info);
    bool FillFileExtInfo(CARTA::FileInfoExtended& extended_info, std::string& hdu, std::string& message);

private:
    CARTA::FileType GetCartaFileType(const std::string& filename);
    bool GetHdf5HduList(CARTA::FileInfo& file_info, const std::string& abs_filename);

    std::string _filename;
    CARTA::FileType _type;
};

} // namespace carta

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
