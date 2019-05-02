#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/images/Images/FITSImage.h>

#include "FileLoader.h"

namespace carta {

class FITSLoader : public FileLoader {
public:
    FITSLoader(const std::string &file);
    ~FITSLoader();

    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    bool getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& getCoordSystem() override;

private:
    std::string file, fitsHdu;
    casacore::FITSImage* image;
};

FITSLoader::FITSLoader(const std::string &filename)
    : file(filename),
      image(nullptr)
{}

FITSLoader::~FITSLoader() {
    if (image != nullptr)
        delete image;
}

void FITSLoader::openFile(const std::string &filename, const std::string &hdu) {
    file = filename;
    fitsHdu = hdu;
    casacore::uInt hdunum(FileInfo::getFITShdu(hdu));
    image = new casacore::FITSImage(filename, 0, hdunum);
}

bool FITSLoader::hasData(FileInfo::Data dl) const {
    switch(dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return ndims >= 2;
        case FileInfo::Data::XYZ:
            return ndims >= 3;
        case FileInfo::Data::XYZW:
            return ndims >= 4;
        case FileInfo::Data::Mask:
            return image->hasPixelMask();
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
// TODO: other loaders don't have this fallback; we should either consistently assume that openFile has been run, or not.
// TODO: in other loaders this is also not an optional property which could be a null pointer.
typename FITSLoader::image_ref FITSLoader::loadData(FileInfo::Data) {
    if (image==nullptr)
        openFile(file, fitsHdu);
    return *image;
}

bool FITSLoader::getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return image->getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& FITSLoader::getCoordSystem() {
    return image->coordinates();
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
