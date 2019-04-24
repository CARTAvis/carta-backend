#pragma once

#include "FileLoader.h"
#include <casacore/images/Images/PagedImage.h>

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string &file);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    bool getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& getCoordSystem() override;

private:
    std::string file;
    casacore::PagedImage<float> image;
};

CasaLoader::CasaLoader(const std::string &filename)
    : file(filename),
      image(filename)
{}

void CasaLoader::openFile(const std::string &filename, const std::string& /*hdu*/) {
    file = filename;
    image = casacore::PagedImage<float>(filename);
}

bool CasaLoader::hasData(FileInfo::Data dl) const {
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
            return image.hasPixelMask();
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
typename CasaLoader::image_ref CasaLoader::loadData(FileInfo::Data ds) {
    return image;
}

bool CasaLoader::getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return image.getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& CasaLoader::getCoordSystem() {
    return image.coordinates();
}

} // namespace carta
