//# FileInfoLoader.h: load FileInfo fields for given file

#ifndef CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_

#include <string>

#include <carta-protobuf/file_info.pb.h>

class FileInfoLoader {
public:
    FileInfoLoader(const std::string& filename);

    bool AddFileInfo(CARTA::FileInfo* file_info);
    bool AddFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);

private:
    // FileInfo
    bool AddFitsHduList(CARTA::FileInfo* file_info, const std::string& abs_filename);
    bool AddHdf5HduList(CARTA::FileInfo* file_info, const std::string& abs_filename);

    std::string _filename;
    CARTA::FileType _type;
};

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
