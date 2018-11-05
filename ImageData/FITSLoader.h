#pragma once

#include "FileLoader.h"
#include <casacore/images/Images/FITSImage.h>
#include <string>
#include <unordered_map>

namespace carta {

class FITSLoader : public FileLoader {
public:
    FITSLoader(const std::string &file);
    ~FITSLoader();

    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    int stokesAxis() override;

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
    case FileInfo::Data::XY:
        return image->shape().size() >= 2;
    case FileInfo::Data::XYZ:
        return image->shape().size() >= 3;
    case FileInfo::Data::XYZW:
        return image->shape().size() >= 4;
    default:
        break;
    }
    return false;
}

typename FITSLoader::image_ref FITSLoader::loadData(FileInfo::Data) {
    if (image==nullptr)
        openFile(file, fitsHdu);
    return *image;
}

int FITSLoader::stokesAxis() {
    casacore::CoordinateSystem csys(image->coordinates());
    casacore::Int coordnum = csys.findCoordinate(casacore::Coordinate::STOKES);
    if (coordnum < 0)
        return -1;
    else
        return csys.pixelAxes(coordnum)(0);
}

} // namespace carta
