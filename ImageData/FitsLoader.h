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
    ImageRef GetImage() override;

private:
    std::string _filename;
    casacore::uInt _hdu;
    std::unique_ptr<casacore::FITSImage> _image;
};

FitsLoader::FitsLoader(const std::string& filename) : _filename(filename) {}

void FitsLoader::OpenFile(const std::string& hdu) {
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));
    if (!_image || (hdu_num != _hdu)) {
        _image.reset(new casacore::FITSImage(_filename, 0, hdu_num));
        _hdu = hdu_num;
        _num_dims = _image->shape().size();
    }
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

typename FitsLoader::ImageRef FitsLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
