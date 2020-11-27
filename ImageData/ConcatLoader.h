#ifndef CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_

#include <casacore/casa/Json/JsonKVMap.h>
#include <casacore/casa/Json/JsonParser.h>
#include <casacore/images/Images/ImageConcat.h>

#include "FileLoader.h"

namespace carta {

class ConcatLoader : public FileLoader {
public:
    ConcatLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::string _filename;
    std::unique_ptr<casacore::ImageConcat<float>> _image;
};

ConcatLoader::ConcatLoader(const std::string& filename) : FileLoader(filename) {}

void ConcatLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        casacore::JsonKVMap _jmap = casacore::JsonParser::parseFile(_filename + "/imageconcat.json");
        _image.reset(new casacore::ImageConcat<float>(_jmap, _filename));
        _num_dims = _image->shape().size();
    }
}

bool ConcatLoader::HasData(FileInfo::Data dl) const {
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

typename ConcatLoader::ImageRef ConcatLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CONCATLOADER_H_
