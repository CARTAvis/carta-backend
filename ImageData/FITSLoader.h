#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/images/Images/FITSImage.h>

#include "FileLoader.h"

namespace carta {

class FITSLoader : public FileLoader {
public:
    FITSLoader(const std::string& file);
    ~FITSLoader();

    void OpenFile(const std::string& file, const std::string& hdu) override;
    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;
    bool GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& GetCoordSystem() override;

private:
    std::string _file, _fits_hdu;
    casacore::FITSImage* _image;
};

FITSLoader::FITSLoader(const std::string& filename) : _file(filename), _image(nullptr) {}

FITSLoader::~FITSLoader() {
    delete _image;
}

void FITSLoader::OpenFile(const std::string& filename, const std::string& hdu) {
    _file = filename;
    _fits_hdu = hdu;
    casacore::uInt hdu_num(FileInfo::GetFitShdu(hdu));
    _image = new casacore::FITSImage(filename, 0, hdu_num);
}

bool FITSLoader::HasData(FileInfo::Data dl) const {
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
            return _image->hasPixelMask();
        default:
            break;
    }
    return false;
}

// TODO: should this check the parameter and fail if it's not the image dataset?
// TODO: other loaders don't have this fallback; we should either consistently assume that openFile has been run, or not.
// TODO: in other loaders this is also not an optional property which could be a null pointer.
typename FITSLoader::ImageRef FITSLoader::LoadData(FileInfo::Data) {
    if (_image == nullptr)
        OpenFile(_file, _fits_hdu);
    return *_image;
}

bool FITSLoader::GetPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    return _image->getMaskSlice(mask, slicer);
}

const casacore::CoordinateSystem& FITSLoader::GetCoordSystem() {
    return _image->coordinates();
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
