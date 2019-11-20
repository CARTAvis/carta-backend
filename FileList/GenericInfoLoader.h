#ifndef CARTA_BACKEND_FILELIST_GENERICINFOLOADER_H_
#define CARTA_BACKEND_FILELIST_GENERICINFOLOADER_H_

#include "../ImageData/Hdf5Attributes.h"
#include "FileInfoLoader.h"

class GenericInfoLoader : public FileInfoLoader {
public:
    GenericInfoLoader(const std::string& filename);

protected:
    CARTA::FileType GetCartaFileType() override;
    virtual bool FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) override;
};

GenericInfoLoader::GenericInfoLoader(const std::string& filename) {
    _filename = filename;
}

CARTA::FileType GenericInfoLoader::GetCartaFileType() {
    return CARTA::FileType::UNKNOWN;
}

bool GenericInfoLoader::FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) {
    message = "Not a region file.";
    return false;
}

#endif // CARTA_BACKEND_FILELIST_GENERICINFOLOADER_H_
