/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/images/Images/FITSImage.h>

//#include "CartaFitsImage.h"
#include "FileLoader.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& file, bool is_gz = false);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    bool _is_gz;
    casacore::uInt _hdu;
    std::unique_ptr<casacore::FITSImage> _image;
};

FitsLoader::FitsLoader(const std::string& filename, bool is_gz) : FileLoader(filename), _is_gz(is_gz) {}

void FitsLoader::OpenFile(const std::string& hdu) {
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));

    if (!_image || (hdu_num != _hdu)) {
        if (_is_gz) {
            // TODO
            throw(casacore::AipsError("Compressed FITS gz image not supported."));
        }

        try {
            _image.reset(new casacore::FITSImage(_filename, 0, hdu_num));
        } catch (const casacore::AipsError& err) {
            //_image.reset(new carta::CartaFitsImage(_filename, 0, hdu_num, compressed));

            // Check for fz (TODO: CartaFitsImage)
			fitsfile* fptr(nullptr);
            int status(0);
            fits_open_file(&fptr, _filename.c_str(), 0, &status);

            // Advance to requested hdu
            for (auto i = 0; i < hdu_num; ++i) {
                int* hdutype(nullptr);
                fits_movrel_hdu(fptr, 1, hdutype, &status);
            }

            bool compressed = fits_is_compressed_image(fptr, &status);
            if (compressed) {
                throw(casacore::AipsError("Compressed FITS fz image not supported."));
            }

            spdlog::error(err.getMesg());
            throw(casacore::AipsError("Error loading FITS image."));
        }

        if (!_image) {
            throw(casacore::AipsError("Error loading FITS image."));
        }

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
