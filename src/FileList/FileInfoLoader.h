/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileInfoLoader.h: load FileInfo fields for given file

#ifndef CARTA_SRC_FILELIST_FILEINFOLOADER_H_
#define CARTA_SRC_FILELIST_FILEINFOLOADER_H_

#include <string>

#include <carta-protobuf/file_info.pb.h>

#include "Cache/FileInfoCache.h"

namespace carta {

class FileInfoLoader {
public:
    FileInfoLoader(const std::string& filename);
    FileInfoLoader(const std::string& filename, const CARTA::FileType& type);

    bool FillFileInfo(CARTA::FileInfo& file_info);

private:
    // The type the caller gave us, or the one the file is asked for on first use. Deciding it
    // probes the file, so a caller that arrives without one pays for the probe only when nothing
    // is cached; see FillFileInfo.
    CARTA::FileType ResolvedType();
    // The expensive half of FillFileInfo: the type, size and image list of a directory image,
    // derived by probing and walking it. Held apart from the rest so FileInfoCache stands in front.
    FileInfoCache::Entry FillDirectoryInfo();
    CARTA::FileType GetCartaFileType(const std::string& filename);
    bool GetHdf5HduList(CARTA::FileInfo& file_info, const std::string& abs_filename);

    std::string _filename;
    CARTA::FileType _type;
    bool _type_known;
};

} // namespace carta

#endif // CARTA_SRC_FILELIST_FILEINFOLOADER_H_
