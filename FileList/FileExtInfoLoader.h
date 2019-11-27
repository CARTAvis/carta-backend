//# FileExtInfoLoader.h: load FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_

#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/file_info.pb.h>

class FileExtInfoLoader {
public:
    FileExtInfoLoader(const std::string& filename);

    bool FillFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);

private:
    // FileInfoExtended
    bool CheckMiriadImage(const std::string& filename, std::string& message);
    bool FillFileInfoFromImage(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);
    void AddShapeEntries(CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis);
    void AddComputedEntries(CARTA::FileInfoExtended* extended_info, casacore::ImageInterface<float>* image);
    std::string MakeAngleString(const std::string& type, double val, const std::string& unit); // convert MVAngle to string

    std::string _filename;
    CARTA::FileType _type;
};

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
