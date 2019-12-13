#ifndef CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CASALOADER_H_

#include <casacore/images/Images/PagedImage.h>

#include "FileLoader.h"

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef LoadData(FileInfo::Data ds) override;

    bool GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) override;
    bool GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer, bool removeDegenerateAxes = false) override;

private:
    casacore::Array<bool> GetMaskSlice(const casacore::Slicer& slicer, bool removeDegenerateAxes = false);

    std::string _filename;
    std::unique_ptr<casacore::PagedImage<float>> _image;
};

CasaLoader::CasaLoader(const std::string& filename) : _filename(filename) {}

void CasaLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        _image.reset(new casacore::PagedImage<float>(_filename));
        _num_dims = _image->shape().size();
    }
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
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            break;
    }
    return false;
}

bool CasaLoader::GetCoordinateSystem(casacore::CoordinateSystem& coord_sys) {
    if (!_image) {
        return false;
    }
    coord_sys = _image->coordinates();
    return true;
}

typename CasaLoader::ImageRef CasaLoader::LoadData(FileInfo::Data ds) {
    if (ds != FileInfo::Data::Image) {
        return nullptr;
    }
    return _image.get(); // nullptr if image not opened
}

bool CasaLoader::GetSlice(casacore::Array<float>& data, const casacore::Slicer& slicer, bool removeDegenerateAxes) {
    if (!_image) {
        return false;
    }

    // Get data slice and apply mask: set unmasked values to NaN
    data = _image->getSlice(slicer, removeDegenerateAxes);
    casacore::Array<bool> mask = GetMaskSlice(slicer, removeDegenerateAxes);
    bool delete_data_ptr;
    float* pData = data.getStorage(delete_data_ptr);
    bool delete_mask_ptr;
    const bool* pMask = mask.getStorage(delete_mask_ptr);
    for (size_t i = 0; i < data.nelements(); ++i) {
        if (!pMask[i]) {
            pData[i] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    mask.freeStorage(pMask, delete_mask_ptr);
    data.putStorage(pData, delete_data_ptr);
    return true;
}

casacore::Array<bool> CasaLoader::GetMaskSlice(const casacore::Slicer& slicer, bool removeDegenerateAxes) {
    casacore::Array<bool> mask;
    if (!_image) {
        return mask;
    }
    return _image->getMaskSlice(slicer, removeDegenerateAxes);
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
