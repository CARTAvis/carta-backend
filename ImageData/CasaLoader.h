#pragma once

#include "FileLoader.h"
#include <casacore/images/Images/PagedImage.h>
#include <string>
#include <unordered_map>

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string &file);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    int stokesAxis() override;

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
    case FileInfo::Data::XY:
        return image.shape().size() >= 2;
    case FileInfo::Data::XYZ:
        return image.shape().size() >= 3;
    case FileInfo::Data::XYZW:
        return image.shape().size() >= 4;
    default:
        break;
    }
    return false;
}

typename CasaLoader::image_ref CasaLoader::loadData(FileInfo::Data) {
    return image;
}

int CasaLoader::stokesAxis() {
    casacore::CoordinateSystem csys(image.coordinates());
    casacore::Int coordnum = csys.findCoordinate(casacore::Coordinate::STOKES);
    if (coordnum < 0)
        return -1;
    else
        return csys.pixelAxes(coordnum)(0);
}

} // namespace carta
