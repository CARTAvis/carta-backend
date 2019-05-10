#ifndef CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CASALOADER_H_

#include <casacore/images/Images/PagedImage.h>

#include "FileLoader.h"

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string& filename);
    void OpenFile(const std::string& filename, const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& GetCoordSystem() override;

private:
    std::string _file;
    casacore::PagedImage<float> _image;
};

CasaLoader::CasaLoader(const std::string& filename) : _file(filename), _image(filename) {}

void CasaLoader::OpenFile(const std::string& filename, const std::string& /*hdu*/) {
    _file = filename;
    _image = casacore::PagedImage<float>(filename);
}

bool CasaLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return _image.hasPixelMask();
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
typename CasaLoader::ImageRef CasaLoader::LoadData(FileInfo::Data ds) {
    return _image;
}

bool CasaLoader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return _image.getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& CasaLoader::GetCoordSystem() {
    return _image.coordinates();
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
