#ifndef CARTA_BACKEND_IMAGEDATA_GENERALLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_GENERALLOADER_H_

#include "FileLoader.h"

namespace carta {

class GeneralLoader : public FileLoader {
public:
    GeneralLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
    void AssignImage(std::shared_ptr<casacore::ImageInterface<float>> image) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::string _filename;
    std::shared_ptr<casacore::ImageInterface<float>> _image;
};

GeneralLoader::GeneralLoader(const std::string& filename) : _filename(filename) {}

void GeneralLoader::OpenFile(const std::string& /*hdu*/) {}

void GeneralLoader::AssignImage(std::shared_ptr<casacore::ImageInterface<float>> image) {
    if (!_image) {
        _image = image;
        _num_dims = _image->shape().size();
    }
}

bool GeneralLoader::HasData(FileInfo::Data dl) const {
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

typename GeneralLoader::ImageRef GeneralLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
