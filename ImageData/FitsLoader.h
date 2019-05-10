#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/images/Images/FITSImage.h>

#include "FileLoader.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& file);
    void OpenFile(const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;

protected:
    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) override;

private:
    std::string _filename;
    casacore::uInt _hdu;
    std::unique_ptr<casacore::FITSImage> _image;
};

FitsLoader::FitsLoader(const std::string& filename) : _filename(filename) {}

void FitsLoader::OpenFile(const std::string& hdu) {
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));
    _image = std::unique_ptr<casacore::FITSImage>(new casacore::FITSImage(_filename, 0, hdu_num));
    _hdu = hdu_num;
    _num_dims = _image->shape().size();
}

bool FitsLoader::HasData(FileInfo::Data dl) const {
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

typename FitsLoader::ImageRef FitsLoader::LoadData(FileInfo::Data ds) {
    if (ds != FileInfo::Data::Image) {
        return nullptr;
    }
    return _image.get(); // nullptr if image not opened
}

bool FitsLoader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    if (_image == nullptr) {
        return false;
    } else {
        return _image->getMaskSlice(mask, slicer);
    }
}

bool FitsLoader::GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) {
    if (_image == nullptr) {
        return false;
    } else {
        coord_sys = _image->coordinates();
        return true;
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
