#ifndef CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_

#include <casacore/images/Images/MIRIADImage.h>

#include "FileLoader.h"

namespace carta {

class MiriadLoader : public FileLoader {
public:
    MiriadLoader(const std::string& file);
    void OpenFile(const std::string& file, const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& GetCoordSystem() override;

private:
    std::string _file;
    casacore::MIRIADImage _image;
};

MiriadLoader::MiriadLoader(const std::string& filename) : _file(filename), _image(filename) {}

void MiriadLoader::OpenFile(const std::string& file, const std::string& hdu /*hdu*/) {
    _file = file;
    _image = casacore::MIRIADImage(file);
}

bool MiriadLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
typename MiriadLoader::ImageRef MiriadLoader::LoadData(FileInfo::Data) {
    return _image;
}

bool MiriadLoader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return _image.getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& MiriadLoader::GetCoordSystem() {
    return _image.coordinates();
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
