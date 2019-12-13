//# FileExtInfoLoader.h: load FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEEXTINFOLOADER_H_

#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/file_info.pb.h>
#include "../ImageData/CartaHdf5Image.h"
#include "../ImageData/FileLoader.h"

class FileExtInfoLoader {
public:
    FileExtInfoLoader(carta::FileLoader* loader);

    bool FillFileExtInfo(CARTA::FileInfoExtended* extended_info, const std::string& filename, const std::string& hdu, std::string& message);

private:
    // FileInfoExtended
    bool FillFileInfoFromImage(CARTA::FileInfoExtended* ext_info, const std::string& hdu, std::string& message);
    void AddMiscInfoHeaders(CARTA::FileInfoExtended* extended_info, const casacore::TableRecord& misc_info);
    void AddShapeEntries(CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis);
    void AddComputedEntries(CARTA::FileInfoExtended* extended_info, casacore::ImageInterface<float>* image, casacore::String& radesys);
    std::string MakeAngleString(const std::string& type, double val, const std::string& unit); // convert MVAngle to string

    carta::FileLoader* _loader;
    CARTA::FileType _type;
};

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
