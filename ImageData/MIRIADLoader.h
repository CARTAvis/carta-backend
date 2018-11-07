#pragma once

#include "FileLoader.h"
#include <casacore/images/Images/MIRIADImage.h>
#include <string>
#include <unordered_map>

namespace carta {

class MIRIADLoader : public FileLoader {
public:
    MIRIADLoader(const std::string &file);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    const casacore::CoordinateSystem& getCoordSystem() override;

private:
    std::string file;
    casacore::MIRIADImage image;
};

MIRIADLoader::MIRIADLoader(const std::string &filename)
    : file(filename),
      image(filename)
{}

void MIRIADLoader::openFile(const std::string &filename, const std::string& /*hdu*/) {
    file = filename;
    image = casacore::MIRIADImage(filename);
}

bool MIRIADLoader::hasData(FileInfo::Data dl) const {
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

typename MIRIADLoader::image_ref MIRIADLoader::loadData(FileInfo::Data) {
    return image;
}

const casacore::CoordinateSystem& MIRIADLoader::getCoordSystem() {
    return image.coordinates();
}

} // namespace carta
