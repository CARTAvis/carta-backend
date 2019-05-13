#ifndef CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_

#include <casacore/images/Images/MIRIADImage.h>

#include "FileLoader.h"

namespace carta {

class MiriadLoader : public FileLoader {
public:
    MiriadLoader(const std::string& file);
    void OpenFile(const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;

protected:
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) override;

private:
    std::string _filename;
    std::unique_ptr<casacore::MIRIADImage> _image;
};

MiriadLoader::MiriadLoader(const std::string& filename) : _filename(filename) {}

void MiriadLoader::OpenFile(const std::string& hdu /*hdu*/) {
    _image = std::unique_ptr<casacore::MIRIADImage>(new casacore::MIRIADImage(_filename));
    _num_dims = _image->shape().size();
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
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            break;
    }
    return false;
}

typename MiriadLoader::ImageRef MiriadLoader::LoadData(FileInfo::Data ds) {
    if (ds != FileInfo::Data::Image) {
        return nullptr;
    }
    return _image.get(); // nullptr if image not opened
}

bool MiriadLoader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    if (_image == nullptr) {
        return false;
    } else {
        return _image->getMaskSlice(mask, slicer);
    }
}

bool MiriadLoader::GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) {
    if (_image == nullptr) {
        return false;
    } else {
        coord_sys = _image->coordinates();
        return true;
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
