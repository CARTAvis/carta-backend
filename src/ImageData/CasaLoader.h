/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
#define CARTA_BACKEND_IMAGEDATA_CASALOADER_H_

#include <casacore/casa/IO/LockFile.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/TempImage.h>

#include "FileLoader.h"
#include "Logger/Logger.h"

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;

private:
    casacore::TempImage<float>* ConvertImageToFloat(casacore::LatticeBase* lattice);
};

CasaLoader::CasaLoader(const std::string& filename) : FileLoader(filename) {}

void CasaLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        // Check if image is locked; PagedImage AutoNoReadLocking option tries to acquire a lock and blocks until it is free
        bool create(false), add_request(false), must_exist(false);
        auto lock_file =
            std::unique_ptr<casacore::LockFile>(new casacore::LockFile(_filename + "/table.lock", 5, create, add_request, must_exist));

        if (!lock_file->canLock(casacore::FileLocker::Read)) {
            throw(casacore::AipsError("Cannot open image, locked by another process."));
        }

        lock_file.reset(nullptr);

        bool converted(false);

        try {
            _image.reset(new casacore::PagedImage<float>(_filename));
        } catch (const casacore::AipsError& err) {
            if (err.getMesg().startsWith("Invalid Table data type")) {
                // Temporary workaround (no data) to support complex images in file browser; must use LEL expression for data
                auto lattice = casacore::ImageOpener::openImage(_filename);
                _image.reset(ConvertImageToFloat(lattice));
                _data_type = lattice->dataType();
                delete lattice;
                converted = true;
            }
        }

        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));

        if (!converted) {
            _data_type = _image->dataType();
        }
    }
}

casacore::TempImage<float>* CasaLoader::ConvertImageToFloat(casacore::LatticeBase* lattice) {
    // Create a TempImage with no data
    if (!lattice) {
        // Not supported by ImageOpener
        throw(casacore::AipsError("Image data type not supported."));
    }

    casacore::TempImage<float>* float_image(nullptr);

    switch (lattice->dataType()) {
        case casacore::TpBool: { // Not supported by ImageOpener
            casacore::PagedImage<bool>* bool_image = dynamic_cast<casacore::PagedImage<bool>*>(lattice);
            float_image = new casacore::TempImage<float>(bool_image->shape(), bool_image->coordinates());
            float_image->setUnits(bool_image->units());
            float_image->setMiscInfo(bool_image->miscInfo());
            float_image->setImageInfo(bool_image->imageInfo());
            break;
        }
        case casacore::TpInt: { // Not supported by ImageOpener
            casacore::PagedImage<int>* int_image = dynamic_cast<casacore::PagedImage<int>*>(lattice);
            float_image = new casacore::TempImage<float>(int_image->shape(), int_image->coordinates());
            float_image->setUnits(int_image->units());
            float_image->setMiscInfo(int_image->miscInfo());
            float_image->setImageInfo(int_image->imageInfo());
            break;
        }
        case casacore::TpDouble: {
            casacore::PagedImage<double>* double_image = dynamic_cast<casacore::PagedImage<double>*>(lattice);
            float_image = new casacore::TempImage<float>(double_image->shape(), double_image->coordinates());
            float_image->setUnits(double_image->units());
            float_image->setMiscInfo(double_image->miscInfo());
            float_image->setImageInfo(double_image->imageInfo());
            break;
        }
        case casacore::TpComplex: {
            casacore::PagedImage<casacore::Complex>* complex_image = dynamic_cast<casacore::PagedImage<casacore::Complex>*>(lattice);
            float_image = new casacore::TempImage<float>(complex_image->shape(), complex_image->coordinates());
            float_image->setUnits(complex_image->units());
            float_image->setMiscInfo(complex_image->miscInfo());
            float_image->setImageInfo(complex_image->imageInfo());
            break;
        }
        case casacore::TpDComplex: {
            casacore::PagedImage<casacore::DComplex>* complex_image = dynamic_cast<casacore::PagedImage<casacore::DComplex>*>(lattice);
            float_image = new casacore::TempImage<float>(complex_image->shape(), complex_image->coordinates());
            float_image->setUnits(complex_image->units());
            float_image->setMiscInfo(complex_image->miscInfo());
            float_image->setImageInfo(complex_image->imageInfo());
            break;
        }
        default:
            throw(casacore::AipsError("Image data type not supported."));
    }

    return float_image;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CASALOADER_H_
