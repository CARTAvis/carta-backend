/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_

#include <casacore/mirlib/miriad.h>

#include "CartaMiriadImage.h"
#include "FileLoader.h"

namespace carta {

template <typename T>
class MiriadLoader : public FileLoader<T> {
public:
    MiriadLoader(const std::string& file);

    bool CanOpenFile(std::string& error) override;

    void OpenFile(const std::string& hdu) override;
};

template <typename T>
MiriadLoader<T>::MiriadLoader(const std::string& filename) : FileLoader<T>(filename) {}

template <typename T>
bool MiriadLoader<T>::CanOpenFile(std::string& error) {
    // Some MIRIAD images throw an error in the miriad libs which cannot be caught in casacore::MIRIADImage, which crashes the backend.
    // If the following checks pass, it should be safe to open the MiriadImage.
    bool miriad_ok(true);
    int t_handle, i_handle, io_stat, num_dim;
    hopen_c(&t_handle, this->_filename.c_str(), "old", &io_stat);
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

template <typename T>
void MiriadLoader<T>::OpenFile(const std::string& /*hdu*/) {
    if (!this->_image) {
        this->_image.reset(new CartaMiriadImage(this->_filename));

        if (!this->_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        this->_image_shape = this->_image->shape();
        this->_num_dims = this->_image_shape.size();
        this->_has_pixel_mask = this->_image->hasPixelMask();
        this->_coord_sys = this->_image->coordinates();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_MIRIADLOADER_H_
