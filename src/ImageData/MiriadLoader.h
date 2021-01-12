/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_

#include "CartaMiriadImage.h"
#include "FileLoader.h"

namespace carta {

class MiriadLoader : public FileLoader {
public:
    MiriadLoader(const std::string& file);

    bool CanOpenFile(std::string& error) override;
    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::unique_ptr<casacore::MIRIADImage> _image;
};

MiriadLoader::MiriadLoader(const std::string& filename) : FileLoader(filename) {}

bool MiriadLoader::CanOpenFile(std::string& error) {
    // Some MIRIAD images throw an error in the miriad libs which cannot be caught in casacore::MIRIADImage, which crashes the backend.
    // If the following checks pass, it should be safe to open the MiriadImage.
    bool miriad_ok(true);
    int t_handle, i_handle, io_stat, num_dim;
    hopen_c(&t_handle, _filename.c_str(), "old", &io_stat);
    if (io_stat != 0) {
        error = "Could not open MIRIAD file";
        miriad_ok = false;
    } else {
        haccess_c(t_handle, &i_handle, "image", "read", &io_stat);
        if (io_stat != 0) {
            error = "Could not open MIRIAD file";
            miriad_ok = false;
        } else {
            rdhdi_c(t_handle, "naxis", &num_dim, 0); // read "naxis" value into ndim, default 0
            hdaccess_c(i_handle, &io_stat);
            hclose_c(t_handle);
            if (num_dim < 2 || num_dim > 4) {
                error = "Image must be 2D, 3D or 4D.";
                miriad_ok = false;
            }
        }
    }
    return miriad_ok;
}

void MiriadLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        _image.reset(new CartaMiriadImage(_filename));
        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }
        _num_dims = _image->shape().size();
    }
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

typename MiriadLoader::ImageRef MiriadLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
