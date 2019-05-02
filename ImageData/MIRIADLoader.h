#ifndef CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_

#include <casacore/images/Images/MIRIADImage.h>

#include "FileLoader.h"

namespace carta {

class MIRIADLoader : public FileLoader {
public:
    MIRIADLoader(const std::string& file);
    void openFile(const std::string& file, const std::string& hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    bool getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& getCoordSystem() override;

private:
    std::string file;
    casacore::MIRIADImage image;
};

MIRIADLoader::MIRIADLoader(const std::string& filename) : file(filename), image(filename) {}

void MIRIADLoader::openFile(const std::string& filename, const std::string& /*hdu*/) {
    file = filename;
    image = casacore::MIRIADImage(filename);
}

bool MIRIADLoader::hasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return ndims >= 2;
        case FileInfo::Data::XYZ:
            return ndims >= 3;
        case FileInfo::Data::XYZW:
            return ndims >= 4;
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
typename MIRIADLoader::image_ref MIRIADLoader::loadData(FileInfo::Data) {
    return image;
}

bool MIRIADLoader::getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return image.getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& MIRIADLoader::getCoordSystem() {
    return image.coordinates();
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
